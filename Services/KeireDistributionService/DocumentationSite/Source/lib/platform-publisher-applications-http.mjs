import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { LocalPublisherApplicationError, saveLocalPublisherApplication } from "./platform-publisher-applications.mjs";

export async function handleLocalPublisherApplicationRequest(authentication, request, siteOrigin, correlationId) {
    const headers = new Headers({
        "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store",
        "vary": "Cookie, Authorization",
        "x-content-type-options": "nosniff",
    });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== "POST") {
        headers.set("allow", "POST");
        return fail("request.method_not_allowed", "Use POST to save a publisher application.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use browser sign-in for this endpoint.", 401);
    }
    const configured = new URL(siteOrigin);
    if (configured.protocol !== "https:" || configured.origin !== siteOrigin || request.headers.get("origin") !== siteOrigin) {
        return fail("request.origin_rejected", "The request origin could not be verified.", 403);
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return fail("account.authentication_required", "Sign in to edit a publisher application.", 401);
    }
    let input;
    try {
        input = await readObject(request, 32768);
    } catch {
        return fail("request.invalid_body", "The application request is invalid or too large.", 400);
    }
    try {
        const application = await saveLocalPublisherApplication(authentication, token, input);
        return respond({ data: application, meta: { apiVersion: "publisher/v1", correlationId } }, 200);
    } catch (error) {
        if (error instanceof LocalPublisherApplicationError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("publisher.unavailable",
            "Saving could not be confirmed. Check your applications before trying again.", 503);
    }
}
