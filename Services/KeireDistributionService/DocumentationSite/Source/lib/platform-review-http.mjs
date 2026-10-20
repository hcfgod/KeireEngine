import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";
import { decideLocalSubmission } from "./platform-review-decision.mjs";

export async function handleLocalReviewRequest(authentication, request, siteOrigin, correlationId) {
    if (new URL(request.url).pathname.replace(/\/+$/, "") !== "/admin/marketplace/v1/actions") {
        return null;
    }
    const headers = new Headers({
        "content-type": "application/json; charset=utf-8", "cache-control": "private, no-store",
        "vary": "Cookie, Authorization", "x-content-type-options": "nosniff",
    });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== "POST") {
        headers.set("allow", "POST");
        return fail("request.method_not_allowed", "Use POST for staff decisions.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use browser sign-in for this endpoint.", 401);
    }
    try {
        const configured = new URL(siteOrigin);
        if (configured.protocol !== "https:" || configured.origin !== siteOrigin
            || request.headers.get("origin") !== siteOrigin) {
            throw new Error("Untrusted origin.");
        }
    } catch {
        return fail("request.origin_rejected", "The request origin could not be verified.", 403);
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return fail("account.authentication_required", "Sign in before reviewing submissions.", 401);
    }
    let input;
    try {
        input = await readObject(request, 65536);
    } catch {
        return fail("request.invalid_body", "The review request is invalid or too large.", 400);
    }
    if (input.operation !== "submission.decide") {
        return fail("staff.operation_unavailable", "This staff operation is not available on this backend yet.", 503);
    }
    try {
        const result = await decideLocalSubmission(authentication, token, {
            submissionId: input.submissionId, decision: input.decision, note: input.decisionNote ?? "",
        });
        return respond({
            data: { submission_id: result.submissionId, submission_state: result.state },
            meta: { apiVersion: "staff/v1", correlationId },
        }, 200);
    } catch (error) {
        if (error instanceof LocalUploadError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("staff.decision_unconfirmed",
            "The decision could not be confirmed. Refresh the submission before trying again.", 503);
    }
}
