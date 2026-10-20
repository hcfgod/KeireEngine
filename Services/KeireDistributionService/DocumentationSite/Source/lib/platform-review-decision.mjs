import { randomUUID } from "node:crypto";
import { LocalUploadError } from "./platform-upload-product.mjs";
import { localReviewDecision } from "./platform-review-policy.mjs";
import { allocatePublicationSequence } from "./platform-publication-sequence.mjs";

export async function decideLocalSubmission(authentication, token, { submissionId, decision, note = "" }) {
    if (typeof submissionId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(submissionId)) {
        throw new LocalUploadError(400, "staff.submission_invalid", "The submission identifier is invalid.");
    }
    const result = await authentication.withSessionTransaction(token, async (connection, session) => {
        const [[staff]] = await connection.execute(
            "SELECT role,active,revoked_at FROM platform_staff_members WHERE user_id=? FOR SHARE", [session.userId]);
        if (session.assurance !== 2 || !staff || ![true, 1].includes(staff.active)
            || staff.revoked_at !== null || !["moderator", "administrator"].includes(staff.role)) {
            throw new LocalUploadError(403, "staff.authorization_required",
                "An active staff role and completed verification are required.");
        }
        const [[submission]] = await connection.execute(
            "SELECT id,state,version_id,validation_report_id FROM marketplace_submissions WHERE id=? FOR UPDATE",
            [submissionId]);
        const [[version]] = submission ? await connection.execute(
            "SELECT id,product_id,archive_sha256 FROM marketplace_product_versions WHERE id=? FOR UPDATE",
            [submission.version_id]) : [[]];
        const [[report]] = submission ? await connection.execute(
            "SELECT * FROM marketplace_validation_reports WHERE id=? FOR SHARE", [submission.validation_report_id]) : [[]];
        const transition = localReviewDecision({ session, staff, submission, report, decision, note });
        if (!version || report.package_sha256 !== version.archive_sha256) {
            throw new LocalUploadError(409, "staff.validation_required",
                "The validation report does not match the release archive.");
        }
        if (transition.queuePublication) {
            const [[existing]] = await connection.execute(
                "SELECT id FROM marketplace_publication_jobs WHERE version_id=? FOR UPDATE", [version.id]);
            if (existing) {
                throw new LocalUploadError(409, "staff.publication_already_exists",
                    "This release already has a publication job. Review its status before approving again.");
            }
            const sequence = await allocatePublicationSequence(connection);
            await connection.execute(`INSERT INTO marketplace_publication_jobs
                (id,submission_id,version_id,approved_by,publication_sequence,publication_expires_at,
                 state,attempt_count,created_at,updated_at)
                VALUES (?,?,?,?,?,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 10 YEAR),'queued',0,
                    UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [randomUUID(), submission.id, version.id, session.userId, sequence]);
        } else if (transition.cancelPublication) {
            await connection.execute(`UPDATE marketplace_publication_jobs SET state='cancelled',
                cancelled_at=UTC_TIMESTAMP(6),cancelled_by=?,lease_owner=NULL,lease_expires_at=NULL,
                updated_at=UTC_TIMESTAMP(6) WHERE version_id=? AND state IN ('queued','leased','retry_wait','failed')`,
            [session.userId, version.id]);
        }
        await connection.execute(`UPDATE marketplace_submissions SET state=?,assigned_to=?,decision_note=?,
            decided_at=CASE WHEN ? THEN UTC_TIMESTAMP(6) ELSE NULL END WHERE id=?`,
        [transition.state, session.userId, transition.note, transition.finalDecision, submission.id]);
        await connection.execute(`UPDATE marketplace_product_versions SET state=?,updated_at=UTC_TIMESTAMP(6)
            WHERE id=?`, [transition.versionState, version.id]);
        await connection.execute(`UPDATE marketplace_products SET state=?,updated_at=UTC_TIMESTAMP(6)
            WHERE id=?`, [transition.productState, version.product_id]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,?,'marketplace_submission',?,?,?)`,
        [session.userId, transition.auditAction, submission.id, randomUUID(),
            JSON.stringify({ decision, previousState: submission.state, versionId: version.id,
                automaticPublicationQueued: transition.queuePublication })]);
        return { submissionId, state: transition.state };
    });
    if (!result) {
        throw new LocalUploadError(401, "account.authentication_required", "Sign in before reviewing submissions.");
    }
    return result;
}
