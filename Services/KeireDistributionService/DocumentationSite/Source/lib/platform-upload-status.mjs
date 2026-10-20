import { LocalUploadError } from "./platform-upload-product.mjs";

/** Return upload progress only through the current publisher membership boundary. */
export async function localUploadStatus(authentication, sessionToken, uploadId) {
    if (typeof uploadId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(uploadId)) {
        throw new LocalUploadError(400, "publisher.upload_invalid", "The upload identifier is invalid.");
    }
    const result = await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
        if (session.assurance !== 2) {
            throw new LocalUploadError(403, "account.mfa_required", "Complete two-step verification to view uploads.");
        }
        const [flags] = await connection.execute(
            "SELECT enabled FROM platform_feature_flags WHERE `key`='publisher_portal_enabled'");
        if (!flags.some((flag) => Boolean(flag.enabled))) {
            throw new LocalUploadError(503, "publisher.disabled", "Publisher uploads are not enabled.");
        }
        const [rows] = await connection.execute(`SELECT upload.id,upload.version_id,upload.state,
            upload.validation_attempts,version.state AS version_state
            FROM marketplace_uploads upload
            JOIN marketplace_product_versions version ON version.id=upload.version_id
            JOIN marketplace_products product ON product.id=version.product_id
            JOIN publishers publisher ON publisher.id=product.publisher_id
            JOIN organizations organization ON organization.id=publisher.organization_id
            JOIN organization_memberships member ON member.organization_id=organization.id
            WHERE upload.id=? AND member.user_id=? AND member.role IN ('owner','admin')
            AND organization.deleted_at IS NULL AND publisher.suspended_at IS NULL FOR SHARE`,
        [uploadId, session.userId]);
        const upload = rows[0];
        if (!upload) {
            throw new LocalUploadError(404, "publisher.upload_not_found", "This upload is unavailable to your account.");
        }
        const [reports] = await connection.execute(`SELECT passed,diagnostics,validator_version,completed_at
            FROM marketplace_validation_reports WHERE upload_id=?
            ORDER BY completed_at DESC,id DESC LIMIT 1`, [upload.id]);
        const report = reports[0];
        let diagnostics = report?.diagnostics;
        if (typeof diagnostics === "string") {
            diagnostics = JSON.parse(diagnostics);
        }
        const completedAt = report?.completed_at;
        return {
            uploadId: upload.id,
            state: upload.state,
            validationAttempts: Number(upload.validation_attempts),
            versionId: upload.version_id,
            versionState: upload.version_state,
            validation: report ? {
                passed: Boolean(report.passed),
                diagnosticCount: Array.isArray(diagnostics) ? diagnostics.length : 0,
                validatorVersion: report.validator_version,
                completedAt: completedAt instanceof Date ? completedAt.toISOString()
                    : typeof completedAt === "string" ? completedAt.replace(" ", "T") + "Z" : null,
            } : null,
        };
    });
    if (!result) {
        throw new LocalUploadError(401, "account.authentication_required", "Sign in again to view uploads.");
    }
    return result;
}
