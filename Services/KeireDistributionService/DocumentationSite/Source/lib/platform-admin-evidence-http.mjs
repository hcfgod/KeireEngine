import { cookieToken } from "./platform-auth-http.mjs";
import { readLocalAdminEvidence } from "./platform-admin-evidence.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

export async function handleLocalAdminEvidenceRequest(service, request) {
    const match = /^\/admin\/marketplace\/v1\/evidence\/([^/]+)\/?$/.exec(new URL(request.url).pathname);
    if (!match) {
        return null;
    }
    const headers = new Headers({ "cache-control": "private, no-store", "vary": "Cookie, Authorization",
        "referrer-policy": "no-referrer", "x-content-type-options": "nosniff", "content-type": "application/json; charset=utf-8",
        "cross-origin-resource-policy": "same-origin", "content-security-policy": "default-src 'none'; sandbox" });
    if (!["GET", "HEAD"].includes(request.method)) {
        headers.set("allow", "GET, HEAD");
        return new Response(null, { status: 405, headers });
    }
    if (request.headers.has("authorization")) {
        return new Response(null, { status: 401, headers });
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return new Response(null, { status: 401, headers });
    }
    try {
        const evidence = await readLocalAdminEvidence(service.authentication, service.adminEvidenceStore, token, match[1]);
        headers.set("content-length", String(evidence.bytes.length));
        headers.set("content-disposition", `attachment; filename="review-${evidence.reportId}.json"`);
        return new Response(request.method === "HEAD" ? null : evidence.bytes, { status: 200, headers });
    } catch (error) {
        const known = error instanceof LocalUploadError;
        return new Response(JSON.stringify({ error: {
            code: known ? error.code : "evidence.unavailable",
            message: known ? error.message : "Private evidence is temporarily unavailable.",
        } }), { status: known ? error.status : 503, headers });
    }
}
