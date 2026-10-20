import { cookieToken } from "./platform-auth-http.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";
import { localUploadStatus } from "./platform-upload-status.mjs";
import { cancelLocalUpload } from "./platform-upload-cancel.mjs";

export async function handleLocalUploadStatusRequest(authentication, request, uploadId, correlationId, siteOrigin) {
    const headers = new Headers({
        "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store",
        "vary": "Cookie, Authorization",
        "x-content-type-options": "nosniff",
    });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (!["GET", "DELETE"].includes(request.method)) {
        headers.set("allow", "GET, DELETE");
        return fail("request.method_not_allowed", "Use GET to read progress or DELETE to cancel an upload.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use browser sign-in for this endpoint.", 401);
    }
    if (request.method === "DELETE") {
        const configured = new URL(siteOrigin);
        if (configured.protocol !== "https:" || configured.origin !== siteOrigin
            || request.headers.get("origin") !== siteOrigin) {
            return fail("request.origin_rejected", "The request origin could not be verified.", 403);
        }
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return fail("account.authentication_required", "Sign in to view upload progress.", 401);
    }
    try {
        const data = request.method === "DELETE"
            ? await cancelLocalUpload(authentication, token, uploadId)
            : await localUploadStatus(authentication, token, uploadId);
        return respond({ data, meta: { apiVersion: "publisher/v1", correlationId } }, 200);
    } catch (error) {
        if (error instanceof LocalUploadError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("publisher.unavailable",
            "The upload operation could not be confirmed. Check its status before trying again.", 503);
    }
}
