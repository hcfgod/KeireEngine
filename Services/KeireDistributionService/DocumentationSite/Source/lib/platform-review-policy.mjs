import { LocalUploadError } from "./platform-upload-product.mjs";

const transitions = Object.freeze({
    in_review: { versionState: "submitted", productState: "submitted" },
    changes_requested: { versionState: "changes_requested", productState: "changes_requested" },
    approved_pending_signature: { versionState: "approved_pending_signature", productState: "approved_pending_signature" },
    rejected: { versionState: "withdrawn", productState: "changes_requested" },
});

/** Call with staff, submission and report rows locked in the authenticated mutation transaction. */
export function localReviewDecision({ session, staff, submission, report, decision, note = "" }) {
    if (session?.assurance !== 2 || !staff || ![true, 1].includes(staff.active)
        || staff.revoked_at !== null || !["moderator", "administrator"].includes(staff.role)) {
        throw new LocalUploadError(403, "staff.authorization_required",
            "An active staff role and completed verification are required.");
    }
    if (!Object.hasOwn(transitions, decision) || typeof note !== "string" || [...note].length > 10000
        || (["changes_requested", "rejected"].includes(decision) && [...note.trim()].length < 3)) {
        throw new LocalUploadError(400, "staff.decision_invalid",
            "Choose a valid decision and provide a reason for requested changes or rejection.");
    }
    const administrator = staff.role === "administrator";
    if (decision === "approved_pending_signature" && !administrator) {
        throw new LocalUploadError(403, "staff.administrator_required",
            "An administrator must approve a release for signing.");
    }
    if (!submission || !["submitted", "in_review", "approved_pending_signature"].includes(submission.state)) {
        throw new LocalUploadError(409, "staff.submission_not_reviewable", "This submission is no longer reviewable.");
    }
    const withdrawApproval = submission.state === "approved_pending_signature";
    if (withdrawApproval && (decision !== "rejected" || !administrator)) {
        throw new LocalUploadError(403, "staff.approval_withdrawal_required",
            "Only an administrator can reject a release already approved for signing.");
    }
    if (!report || ![true, 1].includes(report.passed)) {
        throw new LocalUploadError(409, "staff.validation_required", "A successful validation report is required.");
    }
    if (decision === "approved_pending_signature"
        && (!report.signed_attestation || !report.evidence_sha256 || !report.attestation_key_id)) {
        throw new LocalUploadError(409, "staff.attestation_required",
            "Signed validation evidence is required before approval.");
    }
    return {
        ...transitions[decision],
        state: decision,
        note: note.trim() || null,
        finalDecision: decision !== "in_review",
        queuePublication: decision === "approved_pending_signature",
        cancelPublication: withdrawApproval,
        auditAction: withdrawApproval ? "marketplace.signing_approval_withdrawn" : "marketplace.submission_decided",
    };
}
