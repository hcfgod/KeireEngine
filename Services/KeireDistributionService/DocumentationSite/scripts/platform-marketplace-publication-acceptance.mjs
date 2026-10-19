import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { verifyMarketplaceRecords } from "../Source/lib/platform-marketplace-import.mjs";
import { marketplaceCoreTables } from "../Source/lib/platform-marketplace-schema.mjs";

export async function verifyPublicationConstraints(pool, records) {
    const rollback = new Error("intentional publication acceptance rollback");
    let checks = 0;
    for (const table of ["marketplace_signature_keys", "marketplace_validator_attestation_keys"]) {
        const [keys] = await pool.query(`SELECT public_key_base64,fingerprint FROM ${table}`);
        for (const key of keys) {
            const bytes = Buffer.from(key.public_key_base64, "base64");
            assert.equal(bytes.length, 32);
            assert.equal(bytes.toString("base64"), key.public_key_base64);
            assert.equal(`sha256:${createHash("sha256").update(bytes).digest("hex")}`, key.fingerprint);
            checks++;
        }
    }
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const reject = async (sql, values, code = "ER_CHECK_CONSTRAINT_VIOLATED") => {
            await assert.rejects(connection.execute(sql, values), (error) => error.code === code);
            checks++;
        };
        const [users] = await connection.query("SELECT id FROM platform_accounts LIMIT 1");
        assert.equal(users.length, 1);
        const userId = users[0].id;
        const versionId = randomUUID();
        const versionTable = marketplaceCoreTables.find((table) => table.name === "marketplace_product_versions");
        const names = Object.keys(versionTable.columns);
        const select = names.map((name) => ["id", "version"].includes(name) ? "?" : `\`${name}\``);
        const [inserted] = await connection.execute(`INSERT INTO marketplace_product_versions (${names.map((name) => `\`${name}\``).join(",")})
            SELECT ${select.join(",")} FROM marketplace_product_versions LIMIT 1`, [versionId, `0.0.0-qa.${versionId}`]);
        assert.equal(inserted.affectedRows, 1);
        const uploadId = randomUUID();
        const path = `qa/${uploadId}/é😀.package`;
        const uploadSql = `INSERT INTO marketplace_uploads
            (id,version_id,created_by,storage_path,expected_size_bytes,expected_sha256,state,uploaded_at,
             lease_owner,lease_expires_at,created_at,expires_at,validation_attempts,storage_bucket)
            VALUES (?,?,?,?,4096,?, ?,UTC_TIMESTAMP(6),NULL,NULL,UTC_TIMESTAMP(6),TIMESTAMPADD(HOUR,1,UTC_TIMESTAMP(6)),0,'marketplace-quarantine')`;
        await connection.execute(uploadSql, [uploadId, versionId, userId, path, "a".repeat(64), "uploaded"]);
        const [paths] = await connection.execute("SELECT storage_path FROM marketplace_uploads WHERE id = ?", [uploadId]);
        assert.equal(paths[0].storage_path, path);
        checks++;
        await reject(uploadSql, [randomUUID(), versionId, userId, path, "a".repeat(64), "failed"], "ER_DUP_ENTRY");
        await reject(uploadSql, [randomUUID(), versionId, userId, `qa/${randomUUID()}.package`, "a".repeat(64), "created"], "ER_DUP_ENTRY");
        await reject("UPDATE marketplace_uploads SET storage_path = '../private' WHERE id = ?", [uploadId]);
        await reject("UPDATE marketplace_uploads SET validation_attempts = 6 WHERE id = ?", [uploadId]);

        const reportId = randomUUID();
        await connection.execute(`INSERT INTO marketplace_validation_reports
            (id,upload_id,validator_version,package_sha256,passed,diagnostics,malware_scan_result,code_fingerprint,
             completed_at,manifest_sha256,validator_fingerprint_sha256,policy_version,secret_scan_result,
             managed_validation_result,evidence_storage_path,evidence_sha256,evidence_size_bytes,attestation_key_id,signed_attestation)
            VALUES (?,?,'0.4.7',?,0,?,'clean',NULL,UTC_TIMESTAMP(6),NULL,NULL,'qa','clean','not_applicable',NULL,NULL,NULL,NULL,NULL)`,
        [reportId, uploadId, "a".repeat(64), JSON.stringify([{ message: "Isolated QA", severity: "warning", line: 3 }])]);
        await reject("UPDATE marketplace_validation_reports SET diagnostics = JSON_OBJECT('message','invalid root') WHERE id = ?", [reportId]);
        await reject("UPDATE marketplace_validation_reports SET malware_scan_result = 'ignored' WHERE id = ?", [reportId]);

        const submissionId = randomUUID();
        const submissionSql = `INSERT INTO marketplace_submissions
            (id,version_id,validation_report_id,submitted_by,state,assigned_to,decision_note,submitted_at,decided_at)
            VALUES (?,?,?,?,'submitted',NULL,NULL,UTC_TIMESTAMP(6),NULL)`;
        await connection.execute(submissionSql, [submissionId, versionId, reportId, userId]);
        await reject(submissionSql, [randomUUID(), versionId, reportId, userId], "ER_DUP_ENTRY");
        const jobId = randomUUID();
        await connection.execute(`INSERT INTO marketplace_publication_jobs
            (id,submission_id,version_id,approved_by,signing_key_id,publication_sequence,publication_expires_at,state,attempt_count,
             lease_owner,lease_expires_at,retry_after,last_error_code,created_at,updated_at,published_at,cancelled_at,cancelled_by)
            VALUES (?,?,?,?,NULL,1,TIMESTAMPADD(HOUR,1,UTC_TIMESTAMP(6)),'queued',0,NULL,NULL,NULL,NULL,
                UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),NULL,NULL,NULL)`, [jobId, submissionId, versionId, userId]);
        await reject("UPDATE marketplace_publication_jobs SET state = 'leased' WHERE id = ?", [jobId]);
        await reject("UPDATE marketplace_publication_jobs SET publication_sequence = 0 WHERE id = ?", [jobId]);
        await reject("UPDATE marketplace_publication_jobs SET attempt_count = 6 WHERE id = ?", [jobId]);
        await connection.execute(`UPDATE marketplace_publication_jobs SET state = 'leased', lease_owner = 'qa-worker',
            lease_expires_at = TIMESTAMPADD(MINUTE,5,UTC_TIMESTAMP(6)) WHERE id = ?`, [jobId]);
        checks++;
        await reject("UPDATE marketplace_publication_jobs SET state = 'published' WHERE id = ?", [jobId]);
        throw rollback;
    }), (error) => error === rollback);
    await verifyMarketplaceRecords(pool, records);
    return checks + 1;
}
