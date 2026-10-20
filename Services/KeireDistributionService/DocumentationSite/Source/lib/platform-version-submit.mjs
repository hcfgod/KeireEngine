import { randomUUID } from "node:crypto";
import { LocalUploadError } from "./platform-upload-product.mjs";

export async function submitLocalVersion(authentication, token, versionId) {
    if (typeof versionId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(versionId)) {
        throw new LocalUploadError(400, "publisher.version_invalid", "The package version is invalid.");
    }
    const result = await authentication.withSessionTransaction(token, async (connection, session) => {
        if (session.assurance !== 2) {
            throw new LocalUploadError(403, "auth.mfa_required", "Complete verification before submitting a release.");
        }
        const [flags] = await connection.execute(
            "SELECT enabled FROM platform_feature_flags WHERE `key`='publisher_portal_enabled' FOR SHARE");
        if (!flags.some((flag) => Boolean(flag.enabled))) {
            throw new LocalUploadError(503, "publisher.disabled", "Publisher submissions are not enabled.");
        }
        const [[version]] = await connection.execute(`SELECT version.id,version.product_id,version.archive_sha256
            FROM marketplace_product_versions version
            JOIN marketplace_products product ON product.id=version.product_id
            JOIN publishers publisher ON publisher.id=product.publisher_id
            JOIN organizations organization ON organization.id=publisher.organization_id
            JOIN organization_memberships membership ON membership.organization_id=publisher.organization_id
            WHERE version.id=? AND version.state='validated' AND publisher.suspended_at IS NULL
            AND organization.deleted_at IS NULL AND membership.user_id=? AND membership.role IN ('owner','admin')
            FOR UPDATE`, [versionId, session.userId]);
        if (!version) {
            throw new LocalUploadError(409, "publisher.version_not_submittable",
                "Choose a validated release belonging to a publisher you manage.");
        }
        const [[report]] = await connection.execute(`SELECT report.id FROM marketplace_validation_reports report
            JOIN marketplace_uploads upload ON upload.id=report.upload_id
            WHERE upload.version_id=? AND upload.state='validated' AND report.passed=TRUE
            AND report.package_sha256=? ORDER BY report.completed_at DESC,report.id LIMIT 1 FOR SHARE`,
        [versionId, version.archive_sha256]);
        if (!report) {
            throw new LocalUploadError(409, "publisher.validation_required",
                "This release needs a successful validation report before review.");
        }
        const [[existing]] = await connection.execute(`SELECT id,state FROM marketplace_submissions
            WHERE version_id=? AND state IN ('submitted','in_review','changes_requested','approved_pending_signature')
            FOR UPDATE`, [versionId]);
        if (existing && existing.state !== "changes_requested") {
            throw new LocalUploadError(409, "publisher.submission_already_active", "This release is already in review.");
        }
        const submissionId = existing?.id ?? randomUUID();
        if (existing) {
            await connection.execute(`UPDATE marketplace_submissions SET validation_report_id=?,submitted_by=?,
                state='submitted',assigned_to=NULL,decision_note=NULL,submitted_at=UTC_TIMESTAMP(6),
                decided_at=NULL WHERE id=?`, [report.id, session.userId, submissionId]);
        } else {
            await connection.execute(`INSERT INTO marketplace_submissions
                (id,version_id,validation_report_id,submitted_by,state,submitted_at)
                VALUES (?,?,?,?,'submitted',UTC_TIMESTAMP(6))`, [submissionId, versionId, report.id, session.userId]);
        }
        await connection.execute(`UPDATE marketplace_product_versions SET state='submitted',
            updated_at=UTC_TIMESTAMP(6) WHERE id=?`, [versionId]);
        await connection.execute(`UPDATE marketplace_products SET state='submitted',
            updated_at=UTC_TIMESTAMP(6) WHERE id=?`, [version.product_id]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,'marketplace.submission_created','marketplace_submission',?,?,?)`,
        [session.userId, submissionId, randomUUID(), JSON.stringify({ versionId, validationReportId: report.id })]);
        return { submissionId, state: "submitted" };
    });
    if (!result) {
        throw new LocalUploadError(401, "account.authentication_required", "Sign in before submitting a release.");
    }
    return result;
}
