import { randomUUID } from "node:crypto";
import { LocalUploadError } from "./platform-upload-product.mjs";

export async function cancelLocalUpload(authentication, sessionToken, uploadId) {
    if (typeof uploadId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(uploadId)) {
        throw new LocalUploadError(400, "publisher.upload_invalid", "The upload identifier is invalid.");
    }
    const result = await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
        if (session.assurance !== 2) {
            throw new LocalUploadError(403, "account.mfa_required", "Complete two-step verification to cancel uploads.");
        }
        const [flags] = await connection.execute(
            "SELECT enabled FROM platform_feature_flags WHERE `key`='publisher_portal_enabled' FOR SHARE");
        if (!flags.some((flag) => Boolean(flag.enabled))) {
            throw new LocalUploadError(503, "publisher.disabled", "Publisher uploads are not enabled.");
        }
        const [rows] = await connection.execute(`SELECT upload.id,upload.state,upload.version_id,
            version.state AS version_state FROM marketplace_uploads upload
            JOIN marketplace_product_versions version ON version.id=upload.version_id
            JOIN marketplace_products product ON product.id=version.product_id
            JOIN publishers publisher ON publisher.id=product.publisher_id
            JOIN organizations organization ON organization.id=publisher.organization_id
            JOIN organization_memberships member ON member.organization_id=organization.id
            WHERE upload.id=? AND upload.created_by=? AND member.user_id=?
            AND member.role IN ('owner','admin') AND organization.deleted_at IS NULL
            AND publisher.suspended_at IS NULL FOR UPDATE`, [uploadId, session.userId, session.userId]);
        const upload = rows[0];
        if (!upload) {
            throw new LocalUploadError(404, "publisher.upload_not_found", "This upload is unavailable to your account.");
        }
        if (upload.state === "expired") {
            return { uploadId: upload.id, state: "expired" };
        }
        if (!["created", "uploading"].includes(upload.state) || upload.version_state !== "draft") {
            throw new LocalUploadError(409, "publisher.upload_not_cancellable",
                "This upload has already entered validation and cannot be cancelled.");
        }
        await connection.execute(`UPDATE marketplace_uploads SET state='expired',
            lease_owner=NULL,lease_expires_at=NULL WHERE id=?`, [upload.id]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,'marketplace.upload_cancelled','marketplace_upload',?,?,?)`,
        [session.userId, upload.id, randomUUID(), JSON.stringify({ versionId: upload.version_id })]);
        // Content-addressed files may belong to other releases; cancellation never deletes shared bytes.
        return { uploadId: upload.id, state: "expired" };
    });
    if (!result) {
        throw new LocalUploadError(401, "account.authentication_required", "Sign in again to cancel an upload.");
    }
    return result;
}
