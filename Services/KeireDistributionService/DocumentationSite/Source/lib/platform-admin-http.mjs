import { cookieToken, localBrowserRequestState } from "./platform-auth-http.mjs";
import { localAdminOverview } from "./platform-admin-view.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

export async function handleLocalAdminRequest(service, request, render) {
    if (new URL(request.url).pathname.replace(/\/+$/, "") !== "/admin/marketplace") {
        return null;
    }
    const headers = { "cache-control": "private, no-store", "vary": "Cookie, Authorization",
        "referrer-policy": "no-referrer", "x-content-type-options": "nosniff" };
    if (!["GET", "HEAD"].includes(request.method)) {
        return new Response(null, { status: 405, headers: { ...headers, allow: "GET, HEAD" } });
    }
    if (request.headers.has("authorization")) {
        return new Response(null, { status: 401, headers });
    }
    try {
        const state = await localBrowserRequestState(service.authentication, request);
        if (!state.session || state.session.assurance !== 2) {
            const target = state.session ? "/account/mfa/challenge/" : "/account/sign-in/";
            return new Response(null, { status: 303, headers: { ...headers,
                location: `${target}?next=%2Fadmin%2Fmarketplace%2F` } });
        }
        const adminView = await localAdminOverview(service.authentication,
            cookieToken(request, "__Host-keire-session"));
        adminView.evidenceAvailable = typeof service.adminEvidenceStore?.read === "function";
        const response = await render({ ...state, accountView: null, adminView });
        for (const [name, value] of Object.entries(headers)) {
            response.headers.set(name, value);
        }
        return response;
    } catch (error) {
        const status = error instanceof LocalUploadError ? error.status : 503;
        return new Response(status === 404 ? "Page not found." : "Staff operations are temporarily unavailable. Please sign in again or retry.",
            { status, headers: { ...headers, "content-type": "text/plain; charset=utf-8" } });
    }
}
