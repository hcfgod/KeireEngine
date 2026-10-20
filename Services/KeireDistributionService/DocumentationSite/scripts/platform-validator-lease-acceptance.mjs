import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { leaseValidationUpload, renewValidationLease } from "../Source/lib/platform-validator-leases.mjs";
import { completeValidationUpload } from "../Source/lib/platform-validator-complete.mjs";
import { createValidatorReportFixture } from "./validator-report-fixture.mjs";

/** Connection-local copies prevent the queue test from leasing any imported customer uploads. */
export async function verifyValidatorLeases(pool, fixtureUploadId, fixtureVersionId, evidenceStore) {
    const rawConnection = await pool.getConnection();
    const tables = ["marketplace_uploads", "marketplace_product_versions",
        "marketplace_validation_reports", "platform_audit_events", "marketplace_validator_attestation_keys"];
    const connection = {
        execute: (sql, parameters) => rawConnection.execute(
            sql.replace(/\b(marketplace_uploads|marketplace_product_versions|marketplace_validation_reports|platform_audit_events|marketplace_validator_attestation_keys)\b/g,
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
        for (const table of tables) {
            await rawConnection.execute(`CREATE TEMPORARY TABLE ${table}_qa_lease LIKE ${table}`);
        }
        for (const [table, id] of [["marketplace_uploads", fixtureUploadId],
            ["marketplace_product_versions", fixtureVersionId]]) {
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
        assert.equal(await leaseValidationUpload(isolatedPool, "another-worker", 60), null);
        await assert.rejects(renewValidationLease(isolatedPool, fixtureUploadId, "another-worker", 60), /lease was lost/);
        const renewed = await renewValidationLease(isolatedPool, fixtureUploadId, "validator-fixture", 120);
        assert.ok(Date.parse(renewed.leaseExpiresAt) > Date.parse(lease.leaseExpiresAt));
        const [[active]] = await connection.execute("SELECT state,validation_attempts FROM marketplace_uploads");
        assert.equal(active.state, "leased");
        assert.equal(active.validation_attempts, 1);
        await connection.execute(`UPDATE marketplace_uploads
            SET lease_expires_at=TIMESTAMPADD(SECOND,-1,UTC_TIMESTAMP(6))`);
        await assert.rejects(renewValidationLease(isolatedPool, fixtureUploadId, "validator-fixture", 60), /lease was lost/);
        assert.equal((await leaseValidationUpload(isolatedPool, "replacement-worker", 60)).uploadId, fixtureUploadId);
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
        await leaseValidationUpload(isolatedPool, "completion-worker", 900);
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
        async function* evidenceChunks() {
            yield evidence;
        }
        await evidenceStore.put(evidenceIdentity, evidenceChunks());
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
        const result = await completeValidationUpload(isolatedPool, evidenceStore, fixtureUploadId,
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
        return 31;
    } finally {
        // Closing the connection also removes its private temporary tables, including on assertion failure.
        connection.destroy();
    }
}
