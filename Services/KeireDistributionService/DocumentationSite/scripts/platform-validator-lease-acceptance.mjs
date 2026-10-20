import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { leaseValidationUpload, renewValidationLease } from "../Source/lib/platform-validator-leases.mjs";
import { completeValidationUpload } from "../Source/lib/platform-validator-complete.mjs";
import { createValidatorReportFixture } from "./validator-report-fixture.mjs";
import { authorizeValidatorTransfer, createValidatorCapabilities } from "../Source/lib/platform-validator-capabilities.mjs";
import { createValidatorFileHttp } from "../Source/lib/platform-validator-files.mjs";
import { createQaValidatorHttp } from "./qa-validator-http.mjs";
import { verifyCSharpValidatorHttp } from "./qa-validator-csharp.mjs";
import { verifyVersionSubmission } from "./platform-version-submit-acceptance.mjs";
import { publicationSequenceSchema } from "../Source/lib/platform-publication-sequence.mjs";
import { verifyCSharpPublication } from "./qa-publication-csharp.mjs";

/** Connection-local copies prevent the queue test from leasing any imported customer uploads. */
export async function verifyValidatorLeases(pool, fixtureUploadId, fixtureVersionId, evidenceStore) {
    if (process.env.KEIRE_TEST_PUBLICATION_CSHARP === "1"
        && (process.env.KEIRE_TEST_VALIDATOR_HTTPS !== "1" || process.env.KEIRE_TEST_VALIDATOR_CSHARP !== "1")) {
        throw new Error("C# publication acceptance requires the C# validator HTTPS fixture.");
    }
    const rawConnection = await pool.getConnection();
    const links = createValidatorCapabilities(randomBytes(32), "https://qa.invalid.example");
    let http;
    const tables = ["marketplace_uploads", "marketplace_product_versions",
        "marketplace_validation_reports", "platform_audit_events", "marketplace_validator_attestation_keys",
        "marketplace_products", "marketplace_submissions", "marketplace_publication_jobs",
        "marketplace_publications", "platform_staff_members", "marketplace_signature_keys"];
    const connection = {
        execute: (sql, parameters) => rawConnection.execute(
            sql.replace(/\b(marketplace_uploads|marketplace_product_versions|marketplace_validation_reports|platform_audit_events|marketplace_validator_attestation_keys|marketplace_products|marketplace_submissions|marketplace_publication_jobs|marketplace_publications|platform_staff_members|platform_publication_sequence|marketplace_signature_keys)\b/g,
                (table) => `${table}_qa_lease`), parameters),
        beginTransaction: () => rawConnection.beginTransaction(),
        commit: () => rawConnection.commit(),
        rollback: () => rawConnection.rollback(),
        destroy: () => rawConnection.destroy(),
    };
    const isolatedPool = {
        getConnection: async () => ({
            execute: (...args) => connection.execute(...args),
            beginTransaction: () => connection.beginTransaction(),
            commit: () => connection.commit(),
            rollback: () => connection.rollback(),
            release() {},
            destroy: () => connection.destroy(),
        }),
    };
    try {
        await connection.execute(publicationSequenceSchema.replace("CREATE TABLE IF NOT EXISTS", "CREATE TEMPORARY TABLE"));
        for (const table of tables) {
            await rawConnection.execute(`CREATE TEMPORARY TABLE ${table}_qa_lease LIKE ${table}`);
        }
        const [[originalVersion]] = await rawConnection.execute(
            "SELECT product_id FROM marketplace_product_versions WHERE id=?", [fixtureVersionId]);
        for (const [table, id] of [["marketplace_uploads", fixtureUploadId],
            ["marketplace_product_versions", fixtureVersionId], ["marketplace_products", originalVersion.product_id]]) {
            const [columns] = await rawConnection.execute(`SHOW COLUMNS FROM ${table}`);
            const writable = columns.filter((column) => !/VIRTUAL GENERATED|STORED GENERATED/.test(column.Extra));
            assert.ok(writable.every((column) => /^[a-z_][a-z0-9_]*$/.test(column.Field)));
            const names = writable.map((column) => `\`${column.Field}\``).join(",");
            await rawConnection.execute(`INSERT INTO ${table}_qa_lease (${names})
                SELECT ${names} FROM ${table} WHERE id=?`, [id]);
        }
        await connection.execute(`UPDATE marketplace_uploads SET state='uploaded',validation_attempts=0,
            lease_owner=NULL,lease_expires_at=NULL,expires_at=TIMESTAMPADD(HOUR,1,UTC_TIMESTAMP(6))`);
        await connection.execute("UPDATE marketplace_product_versions SET state='uploaded'");
        const lease = await leaseValidationUpload(isolatedPool, "validator-fixture", 60);
        assert.equal(lease.uploadId, fixtureUploadId);
        assert.equal(lease.versionId, fixtureVersionId);
        assert.equal(lease.evidenceStoragePath, `${fixtureUploadId}/review-evidence-${lease.expectedSha256}.json`);
        assert.ok(Date.parse(lease.leaseExpiresAt) > Date.now());
        assert.equal(lease.validationAttempt, 1);
        const address = links.issue({ uploadId: fixtureUploadId, workerId: "validator-fixture",
            attempt: lease.validationAttempt, purpose: "package" });
        const claims = links.verify(address, "GET");
        assert.equal((await authorizeValidatorTransfer(isolatedPool, claims)).id, fixtureUploadId);
        const files = createValidatorFileHttp({ capabilities: links, pool: isolatedPool,
            packageStore: evidenceStore, evidenceStore });
        const download = await files(new Request(address));
        assert.equal(download.status, 200);
        const packageBytes = Buffer.from(await download.arrayBuffer());
        assert.equal(createHash("sha256").update(packageBytes).digest("hex"), lease.expectedSha256);
        assert.equal(packageBytes.length, lease.expectedSizeBytes);
        assert.equal(await leaseValidationUpload(isolatedPool, "another-worker", 60), null);
        await assert.rejects(renewValidationLease(isolatedPool, fixtureUploadId, "another-worker", 60), /lease was lost/);
        const renewed = await renewValidationLease(isolatedPool, fixtureUploadId, "validator-fixture", 120);
        assert.ok(Date.parse(renewed.leaseExpiresAt) > Date.parse(lease.leaseExpiresAt));
        assert.equal((await authorizeValidatorTransfer(isolatedPool, claims)).id, fixtureUploadId);
        const [[active]] = await connection.execute("SELECT state,validation_attempts FROM marketplace_uploads");
        assert.equal(active.state, "leased");
        assert.equal(active.validation_attempts, 1);
        await connection.execute(`UPDATE marketplace_uploads
            SET lease_expires_at=TIMESTAMPADD(SECOND,-1,UTC_TIMESTAMP(6))`);
        await assert.rejects(renewValidationLease(isolatedPool, fixtureUploadId, "validator-fixture", 60), /lease was lost/);
        await assert.rejects(authorizeValidatorTransfer(isolatedPool, claims), /invalid or expired/);
        assert.equal((await leaseValidationUpload(isolatedPool, "replacement-worker", 60)).uploadId, fixtureUploadId);
        await assert.rejects(authorizeValidatorTransfer(isolatedPool, claims), /invalid or expired/);
        const [[retried]] = await connection.execute("SELECT validation_attempts FROM marketplace_uploads");
        assert.equal(retried.validation_attempts, 2);
        await connection.execute(`UPDATE marketplace_uploads SET validation_attempts=5,
            lease_expires_at=TIMESTAMPADD(SECOND,-1,UTC_TIMESTAMP(6))`);
        assert.equal(await leaseValidationUpload(isolatedPool, "replacement-worker", 60), null);
        const [[exhausted]] = await connection.execute("SELECT state,lease_owner FROM marketplace_uploads");
        assert.equal(exhausted.state, "failed");
        assert.equal(exhausted.lease_owner, null);
        const [[version]] = await connection.execute("SELECT state FROM marketplace_product_versions");
        assert.equal(version.state, "validation_failed");
        const [events] = await connection.execute(`SELECT id FROM platform_audit_events
            WHERE action='marketplace.validation.retry_exhausted'`);
        assert.equal(events.length, 1);
        assert.equal(await leaseValidationUpload(isolatedPool, "replacement-worker", 60), null);
        await connection.execute(`UPDATE marketplace_uploads SET state='uploaded',validation_attempts=0`);
        await connection.execute("UPDATE marketplace_product_versions SET state='uploaded'");
        const brokerSecret = randomBytes(32).toString("hex");
        if (process.env.KEIRE_TEST_VALIDATOR_HTTPS === "1") {
            http = await createQaValidatorHttp({
                pool: isolatedPool, packageStore: evidenceStore, evidenceStore,
                brokerSecret, transferKey: randomBytes(32),
            });
        }
        async function queueRequest(action, extra = {}) {
            const response = await fetch(`${http.origin}/internal/validator/v1/queue/`, {
                method: "POST",
                headers: { "content-type": "application/json", "x-keire-validator-secret": brokerSecret },
                body: JSON.stringify({ action, workerId: "completion-worker", leaseSeconds: 900, ...extra }),
            });
            assert.equal(response.status, 200);
            return (await response.json()).data;
        }
        const completionLease = http
            ? (await queueRequest("lease")).lease
            : await leaseValidationUpload(isolatedPool, "completion-worker", 900);
        if (http) {
            assert.equal(Object.hasOwn(completionLease, "validationAttempt"), false);
            const downloaded = await fetch(completionLease.downloadUrl);
            assert.equal(downloaded.status, 200);
            assert.equal(createHash("sha256").update(Buffer.from(await downloaded.arrayBuffer())).digest("hex"),
                completionLease.expectedSha256);
            assert.ok(Date.parse((await queueRequest("renew", { uploadId: fixtureUploadId })).leaseExpiresAt) > Date.now());
        }
        const [[upload]] = await connection.execute("SELECT * FROM marketplace_uploads");
        const signed = createValidatorReportFixture(upload);
        const evidence = Buffer.from('{"schemaVersion":1,"files":[]}');
        const evidenceIdentity = { sha256: createHash("sha256").update(evidence).digest("hex"), sizeBytes: evidence.length };
        for (const target of [signed.report, signed.document]) {
            target.evidenceSha256 = evidenceIdentity.sha256;
            target.evidenceSizeBytes = evidenceIdentity.sizeBytes;
        }
        signed.seal();
        const key = signed.trustedKey;
        const fingerprint = `sha256:${createHash("sha256").update(Buffer.from(key.public_key_base64, "base64")).digest("hex")}`;
        await connection.execute(`INSERT INTO marketplace_validator_attestation_keys
            (key_id,algorithm,public_key_base64,fingerprint,active,valid_from,valid_until,created_at)
            VALUES (?,?,?,?,TRUE,?,NULL,UTC_TIMESTAMP(6))`,
        [key.key_id, key.algorithm, key.public_key_base64, fingerprint, key.valid_from]);
        await assert.rejects(completeValidationUpload(isolatedPool, evidenceStore, fixtureUploadId,
            "completion-worker", signed.report), /evidence is missing/);
        const evidenceAddress = http ? completionLease.evidenceUploadUrl : links.issue({ uploadId: fixtureUploadId, workerId: "completion-worker",
            attempt: completionLease.validationAttempt, purpose: "evidence" });
        const transfer = (request) => http ? fetch(request) : files(request);
        const evidenceResponse = await transfer(new Request(evidenceAddress, {
            method: "PUT", headers: { "content-type": "application/json", "x-upsert": "false" }, body: evidence,
        }));
        assert.equal(evidenceResponse.status, 200);
        const failingPool = { getConnection: async () => {
            const owned = await isolatedPool.getConnection();
            return { ...owned, execute: async (sql, parameters) => {
                if (sql.includes("INSERT INTO platform_audit_events")) {
                    throw new Error("Synthetic completion audit failure");
                }
                return owned.execute(sql, parameters);
            } };
        } };
        await assert.rejects(completeValidationUpload(failingPool, evidenceStore, fixtureUploadId,
            "completion-worker", signed.report), /Synthetic completion audit failure/);
        const [rolledBackReports] = await connection.execute("SELECT id FROM marketplace_validation_reports");
        assert.equal(rolledBackReports.length, 0);
        const [[rolledBackUpload]] = await connection.execute("SELECT state FROM marketplace_uploads");
        assert.equal(rolledBackUpload.state, "leased");
        const [[rolledBackVersion]] = await connection.execute("SELECT state FROM marketplace_product_versions");
        assert.equal(rolledBackVersion.state, "validating");
        const result = http
            ? await queueRequest("complete", { uploadId: fixtureUploadId, report: signed.report })
            : await completeValidationUpload(isolatedPool, evidenceStore, fixtureUploadId,
                "completion-worker", signed.report);
        const [[completed]] = await connection.execute("SELECT id,passed FROM marketplace_validation_reports");
        assert.equal(result.reportId, completed.id);
        assert.equal(completed.passed, 1);
        const [[publishedIdentity]] = await connection.execute(`SELECT state,archive_sha256,archive_storage_path
            FROM marketplace_product_versions`);
        assert.equal(publishedIdentity.state, "validated");
        assert.equal(publishedIdentity.archive_sha256, upload.expected_sha256);
        assert.equal(publishedIdentity.archive_storage_path, upload.storage_path);
        const noEvidenceRead = { verify: async () => { throw new Error("Receipt must not read evidence again"); } };
        assert.deepEqual(await completeValidationUpload(isolatedPool, noEvidenceRead, fixtureUploadId,
            "completion-worker", signed.report), result);
        await assert.rejects(completeValidationUpload(isolatedPool, noEvidenceRead, fixtureUploadId,
            "different-worker", signed.report), /lease was lost/);
        const [receipts] = await connection.execute(`SELECT id FROM platform_audit_events
            WHERE action='marketplace.validation.completed'`);
        assert.equal(receipts.length, 1);
        assert.equal((await transfer(new Request(evidenceAddress, {
            method: "PUT", headers: { "content-type": "application/json" }, body: evidence,
        }))).status, 403);
        if (http && process.env.KEIRE_TEST_VALIDATOR_CSHARP === "1") {
            await connection.execute("DELETE FROM marketplace_validation_reports");
            await connection.execute("DELETE FROM platform_audit_events");
            await connection.execute("UPDATE marketplace_uploads SET state='uploaded',validation_attempts=0");
            await connection.execute("UPDATE marketplace_product_versions SET state='uploaded'");
            await verifyCSharpValidatorHttp(http, brokerSecret, async (document) => {
                assert.equal(document.algorithm, "Ed25519");
                await connection.execute(`INSERT INTO marketplace_validator_attestation_keys
                    (key_id,algorithm,public_key_base64,fingerprint,active,valid_from,valid_until,created_at)
                    VALUES (?,'ed25519',?,?,TRUE,UTC_TIMESTAMP(6),NULL,UTC_TIMESTAMP(6))`,
                [document.keyId, document.publicKey,
                    `sha256:${createHash("sha256").update(Buffer.from(document.publicKey, "base64")).digest("hex")}`]);
            });
            const [[reportCount]] = await connection.execute("SELECT COUNT(*) AS total FROM marketplace_validation_reports");
            assert.equal(Number(reportCount.total), 1);
            let checks = 53 + await verifyVersionSubmission(isolatedPool, fixtureVersionId);
            if (process.env.KEIRE_TEST_PUBLICATION_CSHARP === "1") {
                checks += await verifyCSharpPublication(isolatedPool, connection, evidenceStore);
            }
            return checks;
        }
        return (http ? 50 : 41) + await verifyVersionSubmission(isolatedPool, fixtureVersionId);
    } finally {
        try {
            await http?.close();
        } finally {
            links.close();
            // Closing the connection also removes its private temporary tables, including on assertion failure.
            connection.destroy();
        }
    }
}
