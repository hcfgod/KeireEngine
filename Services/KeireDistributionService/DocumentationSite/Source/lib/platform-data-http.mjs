import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { requestLocalAccountData, LocalDataRequestError } from "./platform-data-requests.mjs";

export async function handleLocalDataRequest(authentication, request, siteOrigin, correlationId) {
    const headers = new Headers({ "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store", "vary": "Cookie, Authorization", "x-content-type-options": "nosniff" });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== "POST") {
        headers.set("allow", "POST");
        return fail("request.method_not_allowed", "Use POST to request account data.", 405);
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
        return fail("account.authentication_required", "Sign in to request account data.", 401);
    }
    let input;
    try {
        input = await readObject(request, 8192);
    } catch {
        return fail("request.invalid_body", "The data request is invalid or too large.", 400);
    }
    try {
        return respond({ data: await requestLocalAccountData(authentication, token, input.kind) }, 202);
    } catch (error) {
        if (error instanceof LocalDataRequestError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("account.data_request_unavailable", "The request could not be confirmed. Please try again.", 503);
    }
}
