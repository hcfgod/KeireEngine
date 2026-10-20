import { describe, expect, it } from "vitest";
import { localReviewDecision } from "./platform-review-policy.mjs";

function input(overrides = {}) {
    return {
        session: { assurance: 2 }, staff: { role: "administrator", active: 1, revoked_at: null },
        submission: { state: "submitted" },
        report: { passed: 1, signed_attestation: "fixture", evidence_sha256: "a".repeat(64), attestation_key_id: "fixture" },
        decision: "approved_pending_signature", note: "", ...overrides,
    };
}

describe("local staff review policy", () => {
    it("queues signing without publishing the version", () => {
        expect(localReviewDecision(input())).toMatchObject({
            state: "approved_pending_signature", versionState: "approved_pending_signature",
            queuePublication: true, cancelPublication: false,
        });
    });

    it.each([
        { session: { assurance: 1 } },
        { staff: { role: "administrator", active: 0, revoked_at: null } },
        { staff: { role: "administrator", active: 1, revoked_at: "revoked" } },
        { staff: { role: "moderator", active: 1, revoked_at: null } },
        { submission: { state: "approved" } },
        { report: { passed: 0 } },
        { report: { passed: 1 } },
        { decision: "published" },
        { decision: "__proto__" },
        { note: "x".repeat(10001) },
        { decision: "rejected", note: "  " },
    ])("rejects unsafe review input %#", (overrides) => {
        expect(() => localReviewDecision(input(overrides))).toThrow();
    });

    it("allows a moderator to request changes with a reason", () => {
        expect(localReviewDecision(input({
            staff: { role: "moderator", active: 1, revoked_at: null },
            decision: "changes_requested", note: "  Fix the missing files.  ",
        }))).toMatchObject({ versionState: "changes_requested", note: "Fix the missing files.", queuePublication: false });
    });

    it("requires administrator rejection to withdraw signing approval", () => {
        const submission = { state: "approved_pending_signature" };
        expect(() => localReviewDecision(input({ submission, decision: "in_review" }))).toThrow();
        expect(localReviewDecision(input({ submission, decision: "rejected", note: "Approval withdrawn" })))
            .toMatchObject({ cancelPublication: true, versionState: "withdrawn",
                auditAction: "marketplace.signing_approval_withdrawn" });
    });
});
