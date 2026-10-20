import { createHash, randomUUID } from "node:crypto";
import { platformTransaction } from "./platform-mysql.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";
import { verifyPublicationEnvelope } from "./platform-publication-verify.mjs";

function lostLease() {
    return new LocalUploadError(409, "publication.lease_lost", "The publication lease or approval was lost.");
}

/** Host-owned immutable stores must verify both the complete bytes and their content hashes. */
export async function completeLocalPublication(pool, stores, jobId, workerId, signedManifest) {
    if (typeof jobId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(jobId)
        || typeof workerId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(workerId)
        || typeof signedManifest !== "string" || Buffer.byteLength(signedManifest) > 65536) {
        throw new LocalUploadError(400, "publication.request_invalid", "The publication completion request is invalid.");
    }
    const manifestDigest = createHash("sha256").update(signedManifest, "utf8").digest("hex");
    return platformTransaction(pool, async (connection) => {
        const [[job]] = await connection.execute(`SELECT job.*,
            job.lease_expires_at>UTC_TIMESTAMP(6) AS lease_live
            FROM marketplace_publication_jobs job WHERE job.id=? FOR UPDATE`, [jobId]);
        if (!job) {
            throw lostLease();
        }
        // Return only an exact previously committed result, never reinterpret a retry as new publication.
        const [[receipt]] = await connection.execute(`SELECT publication.id AS publication_id,
            publication.version_id,version.product_id,
            DATE_FORMAT(publication.published_at,'%Y-%m-%dT%H:%i:%s.%fZ') AS published_at
            FROM marketplace_publications publication
            JOIN marketplace_product_versions version ON version.id=publication.version_id
            JOIN platform_audit_events audit ON audit.target_id=publication.version_id
                AND audit.action='marketplace.version_published'
            WHERE publication.version_id=? AND publication.signed_manifest=?
                AND JSON_UNQUOTE(JSON_EXTRACT(audit.metadata,'$.publicationJobId'))=?
                AND JSON_UNQUOTE(JSON_EXTRACT(audit.metadata,'$.publicationId'))=publication.id
                AND JSON_UNQUOTE(JSON_EXTRACT(audit.metadata,'$.signedEnvelopeSha256'))=?
                AND JSON_UNQUOTE(JSON_EXTRACT(audit.metadata,'$.workerId'))=? LIMIT 1`,
        [job.version_id, signedManifest, jobId, manifestDigest, workerId]);
        if (receipt) {
            return receipt;
        }
        if (job.state !== "leased" || job.lease_owner !== workerId || Number(job.lease_live) !== 1) {
            throw lostLease();
        }
        const [[release]] = await connection.execute(`SELECT version.id AS version_id,version.product_id,
            version.archive_sha256 AS artifact_sha256,CAST(version.archive_size_bytes AS CHAR) AS artifact_size_bytes,
            version.manifest_sha256,upload.storage_bucket,upload.storage_path,
            report.evidence_sha256,report.evidence_size_bytes
            FROM marketplace_submissions submission
            JOIN marketplace_product_versions version ON version.id=submission.version_id
            JOIN marketplace_products product ON product.id=version.product_id
            JOIN marketplace_validation_reports report ON report.id=submission.validation_report_id
            JOIN marketplace_uploads upload ON upload.id=report.upload_id
            WHERE submission.id=? AND version.id=? AND submission.state='approved_pending_signature'
                AND version.state='approved_pending_signature' AND report.passed=TRUE
                AND upload.state='validated' AND upload.version_id=version.id
                AND report.package_sha256=version.archive_sha256 AND report.manifest_sha256=version.manifest_sha256
                AND upload.expected_sha256=version.archive_sha256 AND upload.expected_size_bytes=version.archive_size_bytes
                AND report.signed_attestation IS NOT NULL AND report.attestation_key_id IS NOT NULL
                AND report.evidence_sha256 IS NOT NULL AND report.evidence_size_bytes IS NOT NULL FOR UPDATE`,
        [job.submission_id, job.version_id]);
        if (!release) {
            throw lostLease();
        }
        const [[key]] = await connection.execute("SELECT * FROM marketplace_signature_keys WHERE key_id=? FOR SHARE",
            [job.signing_key_id]);
        const [[identity]] = await connection.execute(`SELECT signing_key_id,
            CAST(publication_sequence AS CHAR) AS publication_sequence,
            DATE_FORMAT(publication_expires_at,'%Y-%m-%dT%H:%i:%s.%fZ') AS publication_expires_at
            FROM marketplace_publication_jobs WHERE id=?`, [jobId]);
        await verifyPublicationEnvelope(signedManifest, { ...release, ...identity }, key);
        try {
            await stores.packageStore.verify({
                sha256: release.artifact_sha256, sizeBytes: Number(release.artifact_size_bytes),
            });
            await stores.evidenceStore.verify({
                sha256: release.evidence_sha256, sizeBytes: Number(release.evidence_size_bytes),
            });
        } catch {
            throw new LocalUploadError(409, "publication.files_unavailable",
                "The approved package or its review evidence is missing or damaged.");
        }
        // Hashing large packages may outlast a lease. Recheck database time before writing any result.
        const [[current]] = await connection.execute(`SELECT lease_expires_at>UTC_TIMESTAMP(6)
            AND publication_expires_at>UTC_TIMESTAMP(6)
            AND EXISTS (SELECT 1 FROM marketplace_signature_keys WHERE key_id=?
                AND active=TRUE AND algorithm='ed25519' AND valid_from<=UTC_TIMESTAMP(6)
                AND (valid_until IS NULL OR valid_until>UTC_TIMESTAMP(6))) AS lease_live
            FROM marketplace_publication_jobs WHERE id=?`, [job.signing_key_id, jobId]);
        if (Number(current?.lease_live) !== 1) {
            throw lostLease();
        }
        const publicationId = randomUUID();
        await connection.execute(`INSERT INTO marketplace_publications
            (id,version_id,artifact_sha256,manifest_sha256,signature_key_id,signed_manifest,approved_by,published_at)
            VALUES (?,?,?,?,?,?,?,UTC_TIMESTAMP(6))`,
        [publicationId, job.version_id, release.artifact_sha256, release.manifest_sha256,
            job.signing_key_id, signedManifest, job.approved_by]);
        await connection.execute(`UPDATE marketplace_product_versions SET state='published',
            archive_storage_bucket=?,archive_storage_path=?,signature_key_id=?,
            published_at=(SELECT published_at FROM marketplace_publications WHERE id=?),
            updated_at=UTC_TIMESTAMP(6) WHERE id=?`,
        [release.storage_bucket, release.storage_path, job.signing_key_id, publicationId, job.version_id]);
        await connection.execute(`UPDATE marketplace_products SET state='published',
            published_at=COALESCE(published_at,(SELECT published_at FROM marketplace_publications WHERE id=?)),
            updated_at=UTC_TIMESTAMP(6) WHERE id=?`, [publicationId, release.product_id]);
        await connection.execute(`UPDATE marketplace_submissions SET state='approved',
            decided_at=COALESCE(decided_at,UTC_TIMESTAMP(6)) WHERE id=?`, [job.submission_id]);
        await connection.execute(`UPDATE marketplace_publication_jobs SET state='published',
            published_at=(SELECT published_at FROM marketplace_publications WHERE id=?),
            lease_owner=NULL,lease_expires_at=NULL,last_error_code=NULL,retry_after=NULL,
            updated_at=UTC_TIMESTAMP(6) WHERE id=?`, [publicationId, jobId]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,'marketplace.version_published','marketplace_product_version',?,?,?)`,
        [job.approved_by, job.version_id, randomUUID(), JSON.stringify({
            publicationId, publicationJobId: jobId, productId: release.product_id, workerId,
            signedEnvelopeSha256: manifestDigest, artifactSha256: release.artifact_sha256,
            manifestSha256: release.manifest_sha256, signatureKeyId: job.signing_key_id,
            storageBucket: release.storage_bucket, storagePath: release.storage_path,
        })]);
        const [[result]] = await connection.execute(`SELECT id AS publication_id,version_id,
            ? AS product_id,DATE_FORMAT(published_at,'%Y-%m-%dT%H:%i:%s.%fZ') AS published_at
            FROM marketplace_publications WHERE id=?`, [release.product_id, publicationId]);
        return result;
    });
}
