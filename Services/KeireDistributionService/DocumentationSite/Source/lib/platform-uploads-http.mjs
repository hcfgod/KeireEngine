import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";
import { LocalUploadInputError } from "./platform-upload-input.mjs";

export async function handleLocalUploadRequest(service, request, siteOrigin, correlationId) {
    const pathname = new URL(request.url).pathname.replace(/\/+$/, "");
    const reserve = pathname === "/publisher/v1/uploads";
    const operation = /^\/publisher\/v1\/uploads\/([^/]+)\/(prepare|complete)$/.exec(pathname);
    const transfer = /^\/publisher\/v1\/transfer(?:\/[^/]+)?$/.test(pathname);
    if (!reserve && !operation && !transfer) {
        return null;
    }
    const headers = new Headers({
        "content-type": "application/json; charset=utf-8", "cache-control": "private, no-store",
        "vary": "Cookie, Authorization", "x-content-type-options": "nosniff",
    });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (!service.uploads) {
        return fail("publisher.uploads_unavailable", "Package uploads are temporarily unavailable.", 503);
    }
    if (transfer) {
        return service.uploads.handle(request);
    }
    if (request.method !== "POST") {
        headers.set("allow", "POST");
        return fail("request.method_not_allowed", "Use POST for this upload operation.", 405);
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
        return fail("account.authentication_required", "Sign in to upload a package.", 401);
    }
    let input;
    try {
        input = await readObject(request, reserve ? 262144 : 1024);
    } catch {
        return fail("request.invalid_body", "The upload request is invalid or too large.", 400);
    }
    try {
        const data = reserve ? await service.uploads.reserve(token, input)
            : await service.uploads[operation[2]](token, operation[1]);
        return respond({ data, meta: { apiVersion: "publisher/v1", correlationId } }, reserve ? 201 : 200);
    } catch (error) {
        if (error instanceof LocalUploadError || error instanceof LocalUploadInputError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("publisher.upload_unconfirmed",
            "The upload operation could not be confirmed. Check its status before trying again.", 503);
    }
}
