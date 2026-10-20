import { randomUUID } from "node:crypto";
import { LocalUploadError } from "./platform-upload-product.mjs";

async function authorizedUpload(authentication, token, uploadId, action) {
    if (typeof uploadId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(uploadId)) {
        throw new LocalUploadError(400, "publisher.upload_invalid", "The upload identifier is invalid.");
    }
    const result = await authentication.withSessionTransaction(token, async (connection, session) => {
        if (session.assurance !== 2) {
            throw new LocalUploadError(403, "account.mfa_required", "Verify an authenticator before uploading.");
        }
        const [flags] = await connection.execute(
            "SELECT enabled FROM platform_feature_flags WHERE `key`='publisher_portal_enabled' FOR SHARE");
        if (!flags.some((flag) => Boolean(flag.enabled))) {
            throw new LocalUploadError(503, "publisher.disabled", "Publisher uploads are not enabled.");
        }
        const [rows] = await connection.execute(`SELECT upload.id,upload.version_id,upload.state,
            upload.expected_sha256,upload.expected_size_bytes,upload.storage_bucket,upload.expires_at>UTC_TIMESTAMP(6) AS unexpired,
            version.state AS version_state FROM marketplace_uploads upload
            JOIN marketplace_product_versions version ON version.id=upload.version_id
            JOIN marketplace_products product ON product.id=version.product_id
            JOIN publishers publisher ON publisher.id=product.publisher_id
            JOIN organizations organization ON organization.id=publisher.organization_id
            JOIN organization_memberships member ON member.organization_id=organization.id
            WHERE upload.id=? AND upload.created_by=? AND member.user_id=? AND member.role IN ('owner','admin')
            AND organization.deleted_at IS NULL AND publisher.suspended_at IS NULL FOR UPDATE`,
        [uploadId, session.userId, session.userId]);
        const upload = rows[0];
        if (!upload) {
            throw new LocalUploadError(404, "publisher.upload_not_found", "This upload is unavailable to your account.");
        }
        if (!upload.unexpired || !["created", "uploading"].includes(upload.state) || upload.version_state !== "draft") {
            throw new LocalUploadError(409, "publisher.upload_not_writable", "This upload is expired or no longer accepts bytes.");
        }
        return action(connection, session, upload);
    });
    if (!result) {
        throw new LocalUploadError(401, "account.authentication_required", "Sign in again to upload a package.");
    }
    return result;
}

/** Resolve the current identity without retaining database locks while receiving bytes. */
export async function authorizeLocalUpload(authentication, sessionToken, uploadId) {
    return authorizedUpload(authentication, sessionToken, uploadId, async (_connection, _session, upload) => ({
        uploadId: upload.id,
        expectedSha256: upload.expected_sha256,
        expectedSizeBytes: Number(upload.expected_size_bytes),
    }));
}

/** Internal streaming boundary. A verified immutable blob is queued only after a fresh authorization check. */
export async function transferLocalUpload(authentication, sessionToken, uploadId, blobStore, chunks) {
    const identity = await authorizedUpload(authentication, sessionToken, uploadId, async (connection, _session, upload) => {
        await connection.execute("UPDATE marketplace_uploads SET state='uploading' WHERE id=?", [uploadId]);
        return { sha256: upload.expected_sha256, sizeBytes: Number(upload.expected_size_bytes) };
    });
    // Never hold database locks while a client sends a potentially large archive.
    await blobStore.put(identity, chunks);
    return authorizedUpload(authentication, sessionToken, uploadId, async (connection, session, upload) => {
        if (upload.expected_sha256 !== identity.sha256 || Number(upload.expected_size_bytes) !== identity.sizeBytes) {
            throw new LocalUploadError(409, "publisher.upload_changed", "The upload reservation changed during transfer.");
        }
        await connection.execute(
            "UPDATE marketplace_uploads SET state='uploaded',uploaded_at=UTC_TIMESTAMP(6) WHERE id=?", [uploadId]);
        await connection.execute(
            "UPDATE marketplace_product_versions SET state='uploaded',updated_at=UTC_TIMESTAMP(6) WHERE id=?", [upload.version_id]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,'marketplace.upload_completed','marketplace_upload',?,?,?)`,
        [session.userId, uploadId, randomUUID(), JSON.stringify({ versionId: upload.version_id,
            storageBucket: upload.storage_bucket, sizeBytes: identity.sizeBytes, sha256: identity.sha256 })]);
        return { uploadId, versionId: upload.version_id, state: "uploaded" };
    });
}
