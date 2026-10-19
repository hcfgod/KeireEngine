import { createLocalAuthenticationHttp, localBrowserRequestState } from "./platform-auth-http.mjs";
import { localAccountOverview } from "./platform-account-view.mjs";

/** A staged provider adapter. Unported routes fail closed instead of using Supabase identities or data. */
export async function handleLocalPlatformRequest({ backend, request, siteOrigin, clientIdentity, correlationId, render }) {
    const unavailable = (message) => new Response(JSON.stringify({ error: {
        code: "platform.backend_unavailable", message, correlationId,
    } }), { status: 503, headers: {
        "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store",
        "x-content-type-options": "nosniff",
    } });
    if (!backend || backend.provider !== "mysql") {
        return unavailable("The selected platform backend is not initialized.");
    }
    try {
        return await backend.withBackend(async (service) => {
            const handleAuthentication = createLocalAuthenticationHttp(service.authentication, siteOrigin, service.emailAuthentication);
            const response = await handleAuthentication(request, clientIdentity, correlationId);
            if (response) {
                return response;
            }
            const pathname = new URL(request.url).pathname.replace(/\/+$/, "");
            const publicMarketplaceRoute = ["/marketplace", "/marketplace/v1/catalog"].includes(pathname) ||
                /^\/marketplace\/v1\/products\/[^/]+$/.test(pathname);
            if (publicMarketplaceRoute && request.method === "GET") {
                return render({ session: null, pendingChallenge: null, accountView: null, catalogPool: service.pool });
            }
            if (!["/account", "/account/sign-in", "/account/mfa/challenge"].includes(pathname) ||
                !["GET", "HEAD"].includes(request.method)) {
                return unavailable("This platform route is awaiting local-backend migration. Please try again later.");
            }
            const state = await localBrowserRequestState(service.authentication, request);
            const accountView = pathname === "/account" && state.session
                ? await localAccountOverview(service.pool, state.session) : null;
            const rendered = await render({ ...state, accountView, emailAvailable: Boolean(service.emailAuthentication) });
            rendered.headers.set("cache-control", "private, no-store");
            rendered.headers.set("vary", "Cookie, Authorization");
            rendered.headers.set("x-content-type-options", "nosniff");
            rendered.headers.set("x-correlation-id", correlationId);
            rendered.headers.set("referrer-policy", "strict-origin-when-cross-origin");
            return rendered;
        });
    } catch {
        return unavailable("The platform service is temporarily unavailable. Please try again.");
    }
}
