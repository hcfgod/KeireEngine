import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { createLocalOrganization, LocalOrganizationError } from "./platform-organizations.mjs";

export async function handleLocalOrganizationRequest(authentication, request, siteOrigin, correlationId) {
    const headers = new Headers({ "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store", "vary": "Cookie, Authorization", "x-content-type-options": "nosniff" });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== "POST") {
        headers.set("allow", "POST");
        return fail("request.method_not_allowed", "Use POST to create an organization.", 405);
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
        return fail("account.authentication_required", "Sign in to create an organization.", 401);
    }
    let input;
    try {
        input = await readObject(request, 8192);
    } catch {
        return fail("request.invalid_body", "The organization request is invalid or too large.", 400);
    }
    try {
        const organization = await createLocalOrganization(authentication, token, input);
        return respond({ data: organization, meta: { apiVersion: "marketplace/v1", correlationId } }, 201);
    } catch (error) {
        if (error instanceof LocalOrganizationError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("organization.unavailable", "Creation could not be confirmed. Check your organizations before trying again.", 503);
    }
}
