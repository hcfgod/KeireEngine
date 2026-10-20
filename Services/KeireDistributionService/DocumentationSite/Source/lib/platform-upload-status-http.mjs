import { cookieToken } from "./platform-auth-http.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";
import { localUploadStatus } from "./platform-upload-status.mjs";

export async function handleLocalUploadStatusRequest(authentication, request, uploadId, correlationId) {
    const headers = new Headers({
        "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store",
        "vary": "Cookie, Authorization",
        "x-content-type-options": "nosniff",
    });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== "GET") {
        headers.set("allow", "GET");
        return fail("request.method_not_allowed", "Use GET to read upload progress.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use browser sign-in for this endpoint.", 401);
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return fail("account.authentication_required", "Sign in to view upload progress.", 401);
    }
    try {
        const data = await localUploadStatus(authentication, token, uploadId);
        return respond({ data, meta: { apiVersion: "publisher/v1", correlationId } }, 200);
    } catch (error) {
        if (error instanceof LocalUploadError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("publisher.unavailable", "Upload progress is temporarily unavailable. Try again shortly.", 503);
    }
}
