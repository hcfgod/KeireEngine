import assert from "node:assert/strict";
import { randomBytes, randomUUID, createPublicKey, verify } from "node:crypto";
import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { createQaValidatorHttp, createQaPlatformHttp } from "./qa-validator-http.mjs";
import { createLocalPublicationService } from "../Source/lib/platform-publication-service.mjs";
import { publicationSequenceSchema } from "../Source/lib/platform-publication-sequence.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { submitLocalVersion } from "../Source/lib/platform-version-submit.mjs";
import { decideLocalSubmission } from "../Source/lib/platform-review-decision.mjs";
import { runMarketplaceWorkerProcesses } from "./qa-marketplace-worker-processes.mjs";

const tables = ["marketplace_uploads", "marketplace_product_versions", "marketplace_validation_reports",
    "platform_audit_events", "marketplace_validator_attestation_keys", "marketplace_products",
    "marketplace_submissions", "marketplace_publication_jobs", "marketplace_publications",
    "platform_staff_members", "marketplace_signature_keys", "platform_publication_sequence",
    "publishers", "organizations", "organization_memberships", "platform_feature_flags"];
const tablePattern = new RegExp(`\\b(${tables.join("|")})\\b`, "g");

/** Uses connection-private table copies only. Caller supplies synthetic fixture rows and an isolated immutable store. */
export async function verifyActualMarketplaceWorkers(pool, uploadId, versionId, store, configuration) {
    assert.equal(configuration.isolatedFixture, true);
    assert.equal(await store.verify(configuration.packageIdentity).then(() => true), true);
    for (const directory of ["packages", "requests", "reports", "evidence"]) {
        assert.equal((await readdir(path.join(configuration.exchangeRoot, directory))).length, 0,
            "Real worker acceptance requires a fresh empty exchange.");
    }
    const validatorKey = JSON.parse(await readFile(configuration.validatorPublicKey, "utf8"));
    const publicationKey = JSON.parse(await readFile(configuration.publicationPublicKey, "utf8"));
    assert.notEqual(validatorKey.keyId, publicationKey.keyId);
    const raw = await pool.getConnection();
    let previous = Promise.resolve();
    const isolatedPool = {
        async getConnection() {
            const prior = previous;
            let unlock;
            previous = new Promise((resolve) => { unlock = resolve; });
            await prior;
            let released = false;
            return {
                execute: (sql, parameters) => raw.execute(sql.replace(tablePattern, "$1_qa_actual"), parameters),
                beginTransaction: () => raw.beginTransaction(),
                commit: () => raw.commit(),
                rollback: () => raw.rollback(),
                release() {
                    if (!released) {
                        released = true;
                        unlock();
                    }
                },
                destroy() {
                    raw.destroy();
                    if (!released) {
                        released = true;
                        unlock();
                    }
                },
            };
        },
    };
    async function query(sql, parameters) {
        const connection = await isolatedPool.getConnection();
        try {
            return await connection.execute(sql, parameters);
        } finally {
            connection.release();
        }
    }
    let phase = "temporary-schema";
    let validatorHttp;
    let publicationHttp;
    try {
        await raw.execute(publicationSequenceSchema.replace("CREATE TABLE IF NOT EXISTS", "CREATE TEMPORARY TABLE")
            .replace(tablePattern, "$1_qa_actual"));
        for (const table of tables.filter((name) => name !== "platform_publication_sequence")) {
            await raw.execute(`CREATE TEMPORARY TABLE ${table}_qa_actual LIKE ${table}`);
        }
        phase = "synthetic-seed";
        const actorId = randomUUID();
        const organizationId = randomUUID();
        const publisherId = randomUUID();
        const productId = randomUUID();
        await query(`INSERT INTO organizations (id,slug,display_name,created_by,created_at,updated_at)
            VALUES (?,?,'Worker QA',?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [organizationId, `qa-${organizationId}`, actorId]);
        await query(`INSERT INTO organization_memberships (organization_id,user_id,role,created_at)
            VALUES (?,?,'owner',UTC_TIMESTAMP(6))`, [organizationId, actorId]);
        await query(`INSERT INTO publishers (id,organization_id,slug,display_name,summary,verified,created_at,updated_at)
            VALUES (?,?,?,'Worker QA','Isolated worker acceptance',FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [publisherId, organizationId, `qa-${publisherId}`]);
        await query(`INSERT INTO platform_feature_flags (\`key\`,enabled,description,updated_at)
            VALUES ('publisher_portal_enabled',TRUE,'Isolated worker acceptance',UTC_TIMESTAMP(6))`);
        await query(`INSERT INTO marketplace_products
            (id,publisher_id,category_id,slug,display_name,short_description,description_markdown,
             state,license_spdx,license_revision,featured,created_at,updated_at)
            VALUES (?,?,?,'worker-qa','Worker QA','Isolated managed worker acceptance fixture.',
                'Private acceptance only','draft','MIT','qa',FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [productId, publisherId, randomUUID()]);
        await query(`INSERT INTO marketplace_product_versions
            (id,product_id,version,state,install_kind,minimum_engine_version,platforms,architectures,
             renderer_capabilities,release_notes_markdown,created_at,updated_at)
            VALUES (?,?,'0.0.1','uploaded','asset_import','0.4.7','[]','[]','[]',
                'Isolated worker acceptance',UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [versionId, productId]);
        await query(`INSERT INTO marketplace_uploads
            (id,version_id,created_by,storage_path,expected_size_bytes,expected_sha256,state,
             created_at,expires_at,validation_attempts,storage_bucket)
            VALUES (?,?,?,?,?,?,'uploaded',UTC_TIMESTAMP(6),TIMESTAMPADD(HOUR,1,UTC_TIMESTAMP(6)),
                0,'marketplace-quarantine')`, [uploadId, versionId, actorId,
            `qa/${uploadId}/package.keireassetpackage`, configuration.packageIdentity.sizeBytes,
            configuration.packageIdentity.sha256]);
        const [[upload]] = await query("SELECT * FROM marketplace_uploads WHERE id=?", [uploadId]);
        assert.equal(upload.version_id, versionId);
        assert.equal(upload.expected_sha256, configuration.packageIdentity.sha256);
        assert.equal(Number(upload.expected_size_bytes), configuration.packageIdentity.sizeBytes);
        await query(`UPDATE marketplace_uploads SET state='uploaded',uploaded_at=UTC_TIMESTAMP(6),validation_attempts=0,
            lease_owner=NULL,lease_expires_at=NULL,expires_at=TIMESTAMPADD(HOUR,1,UTC_TIMESTAMP(6))`);
        await query("UPDATE marketplace_product_versions SET state='uploaded'");
        for (const [table, key] of [["marketplace_validator_attestation_keys", validatorKey],
            ["marketplace_signature_keys", publicationKey]]) {
            assert.equal(key.algorithm, "Ed25519");
            await query(`INSERT INTO ${table}
                (key_id,algorithm,public_key_base64,fingerprint,active,valid_from,created_at)
                VALUES (?,'ed25519',?,?,TRUE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [key.keyId, key.publicKey, key.fingerprint]);
        }
        phase = "https-services";
        const brokerSecret = randomBytes(32).toString("hex");
        const signerSecret = randomBytes(32).toString("hex");
        validatorHttp = await createQaValidatorHttp({ pool: isolatedPool, packageStore: store,
            evidenceStore: store, brokerSecret, transferKey: randomBytes(32) });
        publicationHttp = await createQaPlatformHttp("publication", (siteOrigin) => createLocalPublicationService({
            pool: isolatedPool, packageStore: store, evidenceStore: store, siteOrigin, queueSecret: signerSecret,
        }));
        async function waitForRow(sql, parameters, controls) {
            while (true) {
                controls.signal.throwIfAborted();
                controls.assertRunning();
                const [[row]] = await query(sql, parameters);
                if (row) {
                    return row;
                }
                await delay(200, undefined, { signal: controls.signal });
            }
        }
        return await runMarketplaceWorkerProcesses({ ...configuration,
            origin: validatorHttp.origin, publicationOrigin: publicationHttp.origin, brokerSecret, signerSecret,
        }, {
            async waitForValidation(controls) {
                phase = "real-validator-report";
                const report = await waitForRow("SELECT * FROM marketplace_validation_reports WHERE upload_id=?", [uploadId], controls);
                assert.equal(Number(report.passed), 1);
                assert.equal(report.malware_scan_result, "clean");
                assert.equal(report.managed_validation_result, "passed");
                assert.match(report.code_fingerprint, /^[0-9a-f]{64}$/);
                assert.equal(report.validator_fingerprint_sha256, configuration.validatorFingerprint);
                return report;
            },
            async approveValidatedVersion() {
                phase = "real-report-review";
                const actor = { userId: upload.created_by, assurance: 2 };
                const authentication = {
                    withSessionTransaction: (_token, action) => platformTransaction(isolatedPool,
                        (connection) => action(connection, actor)),
                };
                const submission = await submitLocalVersion(authentication, "isolated-fixture", versionId);
                await query(`INSERT INTO platform_staff_members (user_id,role,active,appointed_at,updated_at)
                    VALUES (?,'administrator',TRUE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [actor.userId]);
                await decideLocalSubmission(authentication, "isolated-fixture", {
                    submissionId: submission.submissionId, decision: "approved_pending_signature",
                });
            },
            waitForPublication(controls) {
                phase = "real-signer-publication";
                return waitForRow("SELECT * FROM marketplace_publications WHERE version_id=?", [versionId], controls);
            },
            async verifyPublication(publication) {
                phase = "persisted-signature";
                assert.equal(publication.signature_key_id, publicationKey.keyId);
                const [[job]] = await query("SELECT state FROM marketplace_publication_jobs WHERE version_id=?", [versionId]);
                assert.equal(job.state, "published");
                const [[version]] = await query("SELECT state FROM marketplace_product_versions WHERE id=?", [versionId]);
                assert.equal(version.state, "published");
                const [[receipts]] = await query("SELECT COUNT(*) AS total FROM platform_audit_events WHERE action='marketplace.version_published'");
                assert.equal(Number(receipts.total), 1);
                const envelope = JSON.parse(publication.signed_manifest);
                assert.equal(envelope.signature.keyId, publicationKey.keyId);
                const publicKey = createPublicKey({ format: "der", type: "spki", key: Buffer.concat([
                    Buffer.from("302a300506032b6570032100", "hex"), Buffer.from(publicationKey.publicKey, "base64"),
                ]) });
                assert.ok(verify(null, Buffer.from(envelope.document), publicKey,
                    Buffer.from(envelope.signature.value, "base64")), "Persisted publication signature failed verification.");
                const document = JSON.parse(envelope.document);
                assert.equal(document.versionId, versionId);
                assert.equal(document.artifactSha256, configuration.packageIdentity.sha256);
                assert.equal(document.artifactSizeBytes, configuration.packageIdentity.sizeBytes);
            },
        });
    } catch (error) {
        error.fixtureStage ??= phase;
        throw error;
    } finally {
        try {
            await Promise.all([validatorHttp?.close(), publicationHttp?.close()]);
        } finally {
            raw.destroy();
        }
    }
}
