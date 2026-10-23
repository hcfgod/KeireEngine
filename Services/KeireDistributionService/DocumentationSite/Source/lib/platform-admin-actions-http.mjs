import { readLocalAdminEvidence } from "./platform-admin-evidence.mjs";
import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { applyLocalAdminAction } from "./platform-admin-actions.mjs";
import { decideLocalSubmission } from "./platform-review-decision.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

export async function handleLocalAdminActionsRequest(authentication, request, siteOrigin, correlationId, evidenceStore = null) {
    if (new URL(request.url).pathname.replace(/\/+$/, "") !== "/admin/marketplace/v1/actions") {
        return null;
    }
    const headers = new Headers({ "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store", "vary": "Cookie, Authorization", "x-content-type-options": "nosniff" });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== "POST") {
        headers.set("allow", "POST");
        return fail("request.method_not_allowed", "Use POST for staff changes.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use browser sign-in for this endpoint.", 401);
    }
    try {
        const configured = new URL(siteOrigin);
        if (configured.protocol !== "https:" || configured.origin !== siteOrigin
            || new URL(request.url).origin !== siteOrigin || request.headers.get("origin") !== siteOrigin) {
            throw new Error("Untrusted origin.");
        }
    } catch {
        return fail("request.origin_rejected", "The request origin could not be verified.", 403);
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return fail("account.authentication_required", "Sign in before making staff changes.", 401);
    }
    let input;
    try {
        input = await readObject(request, 65536);
    } catch {
        return fail("request.invalid_body", "The staff request is invalid or too large.", 400);
    }
    try {
        let data;
        if (input.operation === "submission.decide") {
            const result = await decideLocalSubmission(authentication, token, {
                submissionId: input.submissionId, decision: input.decision, note: input.decisionNote ?? "",
            });
            data = { submission_id: result.submissionId, submission_state: result.state };
        } else if (input.operation === "evidence.issue") {
            const evidence = await readLocalAdminEvidence(authentication, evidenceStore, token, input.reportId);
            data = { reportId: evidence.reportId, url: `/admin/marketplace/v1/evidence/${evidence.reportId}/`,
                sha256: evidence.identity.sha256, sizeBytes: evidence.identity.sizeBytes,
                attestationKeyId: evidence.keyId, attestationFingerprint: evidence.fingerprint,
                requiresBrowserSession: true };
        } else {
            data = await applyLocalAdminAction(authentication, token, input);
        }
        return respond({ data, meta: { apiVersion: "staff/v1", correlationId } }, 200);
    } catch (error) {
        if (error instanceof LocalUploadError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("staff.change_unconfirmed", "The change could not be confirmed. Refresh before trying again.", 503);
    }
}
