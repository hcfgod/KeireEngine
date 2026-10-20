import { randomUUID } from "node:crypto";
import { platformTransaction } from "./platform-mysql.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

function validateWorker(workerId, leaseSeconds) {
    if (typeof workerId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(workerId)
        || !Number.isInteger(leaseSeconds) || leaseSeconds < 60 || leaseSeconds > 900) {
        throw new LocalUploadError(400, "publication.worker_invalid", "The publication worker request is invalid.");
    }
}

export async function leaseLocalPublication(pool, workerId, signingKeyId, leaseSeconds = 300) {
    validateWorker(workerId, leaseSeconds);
    if (typeof signingKeyId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(signingKeyId)) {
        throw new LocalUploadError(400, "publication.key_invalid", "The publication signing key is invalid.");
    }
    return platformTransaction(pool, async (connection) => {
        const [[key]] = await connection.execute(`SELECT key_id FROM marketplace_signature_keys
            WHERE key_id=? AND active=TRUE AND algorithm='ed25519' AND valid_from<=UTC_TIMESTAMP(6)
            AND (valid_until IS NULL OR valid_until>UTC_TIMESTAMP(6)) FOR SHARE`, [signingKeyId]);
        if (!key) {
            throw new LocalUploadError(403, "publication.key_unavailable", "The publication signing key is not active.");
        }
        const [expired] = await connection.execute(`SELECT id,attempt_count FROM marketplace_publication_jobs
            WHERE state='leased' AND lease_expires_at<=UTC_TIMESTAMP(6)
            ORDER BY lease_expires_at,id LIMIT 50 FOR UPDATE SKIP LOCKED`);
        for (const job of expired) {
            const exhausted = Number(job.attempt_count) >= 5;
            await connection.execute(`UPDATE marketplace_publication_jobs SET state=?,lease_owner=NULL,
                lease_expires_at=NULL,retry_after=CASE WHEN ? THEN NULL ELSE DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 5 MINUTE) END,
                last_error_code='publication.lease_expired',updated_at=UTC_TIMESTAMP(6) WHERE id=?`,
            [exhausted ? "failed" : "retry_wait", exhausted, job.id]);
        }
        const [[job]] = await connection.execute(`SELECT job.id,job.version_id,job.attempt_count
            FROM marketplace_publication_jobs job
            JOIN marketplace_submissions submission ON submission.id=job.submission_id
            JOIN marketplace_product_versions version ON version.id=job.version_id
            WHERE job.state IN ('queued','retry_wait') AND (job.retry_after IS NULL OR job.retry_after<=UTC_TIMESTAMP(6))
            AND job.attempt_count<5 AND job.publication_expires_at>UTC_TIMESTAMP(6)
            AND submission.version_id=version.id AND submission.state='approved_pending_signature'
            AND version.state='approved_pending_signature'
            ORDER BY job.created_at,job.id LIMIT 1 FOR UPDATE SKIP LOCKED`);
        if (!job) {
            return null;
        }
        const [[metadata]] = await connection.execute(`SELECT job.id AS job_id,version.id AS version_id,
            version.product_id,upload.storage_bucket,upload.storage_path,version.archive_sha256 AS artifact_sha256,
            version.archive_size_bytes AS artifact_size_bytes,version.manifest_sha256,
            CAST(job.publication_sequence AS CHAR) AS publication_sequence,
            DATE_FORMAT(job.publication_expires_at,'%Y-%m-%dT%H:%i:%s.%fZ') AS publication_expires_at,
            report.signed_attestation AS validation_attestation,report.attestation_key_id,
            report.evidence_storage_path,report.evidence_sha256,report.evidence_size_bytes
            FROM marketplace_publication_jobs job
            JOIN marketplace_submissions submission ON submission.id=job.submission_id
            JOIN marketplace_product_versions version ON version.id=job.version_id
            JOIN marketplace_validation_reports report ON report.id=submission.validation_report_id
            JOIN marketplace_uploads upload ON upload.id=report.upload_id
            WHERE job.id=? AND report.passed=TRUE AND upload.state='validated' AND upload.version_id=version.id
            AND report.package_sha256=version.archive_sha256
            AND upload.expected_sha256=version.archive_sha256 AND upload.expected_size_bytes=version.archive_size_bytes
            AND report.signed_attestation IS NOT NULL AND report.attestation_key_id IS NOT NULL
            AND report.evidence_sha256 IS NOT NULL FOR SHARE`, [job.id]);
        if (!metadata) {
            throw new LocalUploadError(409, "publication.evidence_unavailable",
                "The approved release is missing matching validation evidence.");
        }
        await connection.execute(`UPDATE marketplace_publication_jobs SET state='leased',signing_key_id=?,lease_owner=?,
            lease_expires_at=TIMESTAMPADD(SECOND,?,UTC_TIMESTAMP(6)),retry_after=NULL,attempt_count=attempt_count+1,
            last_error_code=NULL,updated_at=UTC_TIMESTAMP(6) WHERE id=?`, [signingKeyId, workerId, leaseSeconds, job.id]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),'marketplace.publication.leased','marketplace_publication_job',?,?,?)`,
        [job.id, randomUUID(), JSON.stringify({ workerId, signingKeyId, attempt: Number(job.attempt_count) + 1 })]);
        // Keep the sequence as decimal text internally; the HTTP boundary must preserve its exact JSON integer.
        return metadata;
    });
}

export async function renewLocalPublication(pool, jobId, workerId, leaseSeconds = 300) {
    validateWorker(workerId, leaseSeconds);
    if (typeof jobId !== "string" || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(jobId)) {
        throw new LocalUploadError(400, "publication.job_invalid", "The publication job identifier is invalid.");
    }
    return platformTransaction(pool, async (connection) => {
        const [[job]] = await connection.execute(`SELECT job.id FROM marketplace_publication_jobs job
            JOIN marketplace_submissions submission ON submission.id=job.submission_id
            JOIN marketplace_product_versions version ON version.id=job.version_id
            WHERE job.id=? AND job.state='leased' AND job.lease_owner=? AND job.lease_expires_at>UTC_TIMESTAMP(6)
            AND submission.state='approved_pending_signature' AND version.state='approved_pending_signature'
            FOR UPDATE`, [jobId, workerId]);
        if (!job) {
            throw new LocalUploadError(409, "publication.lease_lost", "The publication lease was lost.");
        }
        await connection.execute(`UPDATE marketplace_publication_jobs
            SET lease_expires_at=TIMESTAMPADD(SECOND,?,UTC_TIMESTAMP(6)),updated_at=UTC_TIMESTAMP(6) WHERE id=?`,
        [leaseSeconds, jobId]);
        const [[deadline]] = await connection.execute(`SELECT
            DATE_FORMAT(lease_expires_at,'%Y-%m-%dT%H:%i:%s.%fZ') AS leaseExpiresAt
            FROM marketplace_publication_jobs WHERE id=?`, [jobId]);
        return deadline;
    });
}
