import { randomUUID } from "node:crypto";
import { platformTransaction } from "./platform-mysql.mjs";

const maximumAttempts = 5;
const recoveryBatchSize = 50;

function validateWorker(workerId, leaseSeconds) {
    if (typeof workerId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(workerId)
        || !Number.isInteger(leaseSeconds) || leaseSeconds < 60 || leaseSeconds > 1800) {
        throw new Error("The validator lease parameters are invalid.");
    }
}

async function audit(connection, action, uploadId, metadata) {
    await connection.execute(`INSERT INTO platform_audit_events
        (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
        VALUES (UTC_TIMESTAMP(6),NULL,NULL,?,'marketplace_upload',?,?,?)`,
    [action, uploadId, randomUUID(), JSON.stringify(metadata)]);
}

async function recoverExpiredLeases(connection) {
    const [expired] = await connection.execute(`SELECT upload.id,upload.version_id,
        upload.validation_attempts,version.state AS version_state FROM marketplace_uploads upload
        JOIN marketplace_product_versions version ON version.id=upload.version_id
        WHERE upload.state='leased' AND upload.lease_expires_at<=UTC_TIMESTAMP(6)
        ORDER BY upload.lease_expires_at,upload.id LIMIT ${recoveryBatchSize} FOR UPDATE SKIP LOCKED`);
    for (const upload of expired) {
        const exhausted = Number(upload.validation_attempts) >= maximumAttempts;
        await connection.execute(`UPDATE marketplace_uploads SET state=?,lease_owner=NULL,
            lease_expires_at=NULL WHERE id=?`, [exhausted ? "failed" : "uploaded", upload.id]);
        if (upload.version_state === "validating") {
            await connection.execute(`UPDATE marketplace_product_versions SET state=?,updated_at=UTC_TIMESTAMP(6)
                WHERE id=?`, [exhausted ? "validation_failed" : "uploaded", upload.version_id]);
            if (exhausted) {
                await audit(connection, "marketplace.validation.retry_exhausted", upload.id,
                    { attempts: maximumAttempts });
            }
        }
    }
}

/** Internal broker service only. The HTTP boundary must authenticate the dedicated broker credential first. */
export async function leaseValidationUpload(pool, workerId, leaseSeconds = 900) {
    validateWorker(workerId, leaseSeconds);
    return platformTransaction(pool, async (connection) => {
        await recoverExpiredLeases(connection);
        const [rows] = await connection.execute(`SELECT upload.id,upload.version_id,upload.storage_bucket,
            upload.storage_path,upload.expected_size_bytes,upload.expected_sha256 FROM marketplace_uploads upload
            JOIN marketplace_product_versions version ON version.id=upload.version_id
            WHERE upload.state='uploaded' AND upload.uploaded_at IS NOT NULL
            AND upload.expires_at>UTC_TIMESTAMP(6) AND upload.validation_attempts<${maximumAttempts}
            AND version.state IN ('draft','uploaded','validation_failed')
            AND NOT EXISTS (SELECT 1 FROM marketplace_validation_reports report WHERE report.upload_id=upload.id)
            ORDER BY upload.uploaded_at,upload.id LIMIT 1 FOR UPDATE SKIP LOCKED`);
        const upload = rows[0];
        if (!upload) {
            return null;
        }
        await connection.execute(`UPDATE marketplace_uploads SET state='leased',lease_owner=?,
            lease_expires_at=TIMESTAMPADD(SECOND,?,UTC_TIMESTAMP(6)),
            validation_attempts=validation_attempts+1 WHERE id=?`, [workerId, leaseSeconds, upload.id]);
        await connection.execute(`UPDATE marketplace_product_versions SET state='validating',
            updated_at=UTC_TIMESTAMP(6) WHERE id=?`, [upload.version_id]);
        await audit(connection, "marketplace.validation.leased", upload.id,
            { workerId, leaseSeconds, storageBucket: upload.storage_bucket });
        const [[deadline]] = await connection.execute(`SELECT
            DATE_FORMAT(lease_expires_at,'%Y-%m-%dT%H:%i:%s.%fZ') AS expires_at
            FROM marketplace_uploads WHERE id=?`, [upload.id]);
        return {
            uploadId: upload.id,
            versionId: upload.version_id,
            storageBucket: upload.storage_bucket,
            storagePath: upload.storage_path,
            evidenceStoragePath: `${upload.id}/review-evidence-${upload.expected_sha256}.json`,
            expectedSizeBytes: Number(upload.expected_size_bytes),
            expectedSha256: upload.expected_sha256,
            leaseExpiresAt: deadline.expires_at,
        };
    });
}

export async function renewValidationLease(pool, uploadId, workerId, leaseSeconds = 900) {
    validateWorker(workerId, leaseSeconds);
    if (typeof uploadId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(uploadId)) {
        throw new Error("The validator upload identifier is invalid.");
    }
    return platformTransaction(pool, async (connection) => {
        const [rows] = await connection.execute(`SELECT id FROM marketplace_uploads
            WHERE id=? AND state='leased' AND lease_owner=? AND lease_expires_at>UTC_TIMESTAMP(6)
            FOR UPDATE`, [uploadId, workerId]);
        if (!rows.length) {
            throw new Error("The validation lease was lost.");
        }
        await connection.execute(`UPDATE marketplace_uploads SET
            lease_expires_at=TIMESTAMPADD(SECOND,?,UTC_TIMESTAMP(6)) WHERE id=?`, [leaseSeconds, uploadId]);
        const [[deadline]] = await connection.execute(`SELECT
            DATE_FORMAT(lease_expires_at,'%Y-%m-%dT%H:%i:%s.%fZ') AS expires_at
            FROM marketplace_uploads WHERE id=?`, [uploadId]);
        return { leaseExpiresAt: deadline.expires_at };
    });
}
