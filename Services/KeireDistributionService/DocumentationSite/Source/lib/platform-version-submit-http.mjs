import { cookieToken } from "./platform-auth-http.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";
import { submitLocalVersion } from "./platform-version-submit.mjs";

export async function handleLocalVersionSubmit(authentication, request, siteOrigin, correlationId) {
    const match = /^\/publisher\/v1\/versions\/([^/]+)\/submit\/?$/.exec(new URL(request.url).pathname);
    if (!match) {
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
        return fail("request.method_not_allowed", "Use POST to submit a release for review.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use browser sign-in for this endpoint.", 401);
    }
    let trustedOrigin = false;
    try {
        const configured = new URL(siteOrigin);
        trustedOrigin = configured.protocol === "https:" && configured.origin === siteOrigin
            && request.headers.get("origin") === siteOrigin;
    } catch {
        // Invalid host configuration must never admit a browser mutation.
    }
    if (!trustedOrigin) {
        return fail("request.origin_rejected", "The request origin could not be verified.", 403);
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return fail("account.authentication_required", "Sign in before submitting a release.", 401);
    }
    try {
        const data = await submitLocalVersion(authentication, token, match[1]);
        return respond({ data, meta: { apiVersion: "publisher/v1", correlationId } }, 201);
    } catch (error) {
        if (error instanceof LocalUploadError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("publisher.submission_unconfirmed",
            "Submission could not be confirmed. Check the release status before trying again.", 503);
    }
}
