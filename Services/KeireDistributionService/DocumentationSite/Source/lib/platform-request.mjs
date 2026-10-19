import { createLocalAuthenticationHttp, localBrowserRequestState } from "./platform-auth-http.mjs";
import { localAccountOverview } from "./platform-account-view.mjs";
import { localProductPage } from "./platform-product.mjs";
import { handleLocalClaimRequest } from "./platform-claims-http.mjs";
import { handleLocalRecoveryRequest } from "./platform-recovery-http.mjs";
import { handleLocalOrganizationRequest } from "./platform-organizations-http.mjs";

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
            const recoveryResponse = await handleLocalRecoveryRequest(service, request, siteOrigin, clientIdentity, correlationId);
            if (recoveryResponse) {
                return recoveryResponse;
            }
            const handleAuthentication = createLocalAuthenticationHttp(service.authentication, siteOrigin, service.emailAuthentication);
            const response = await handleAuthentication(request, clientIdentity, correlationId);
            if (response) {
                return response;
            }
            const pathname = new URL(request.url).pathname.replace(/\/+$/, "");
            if (["/account/recover", "/account/update-password"].includes(pathname) && ["GET", "HEAD"].includes(request.method)) {
                const rendered = await render({ session: null, pendingChallenge: null, accountView: null,
                    localRecovery: true });
                rendered.headers.set("cache-control", "private, no-store");
                rendered.headers.set("referrer-policy", "no-referrer");
                rendered.headers.set("vary", "Cookie, Authorization");
                return rendered;
            }
            if (pathname === "/marketplace/v1/claims") {
                return handleLocalClaimRequest(service.authentication, request, siteOrigin, clientIdentity, correlationId);
            }
            if (pathname === "/marketplace/v1/organizations") {
                return handleLocalOrganizationRequest(service.authentication, request, siteOrigin, correlationId);
            }
            if (pathname === "/marketplace/v1/library" && request.method === "GET") {
                const state = await localBrowserRequestState(service.authentication, request);
                const rendered = await render({ ...state, accountView: null,
                    marketplaceContext: { pool: service.pool, session: state.session } });
                rendered.headers.set("cache-control", "private, no-store");
                rendered.headers.set("vary", "Cookie, Authorization");
                return rendered;
            }
            const publicMarketplaceRoute = ["/marketplace", "/marketplace/v1/catalog"].includes(pathname) ||
                /^\/marketplace\/v1\/products\/[^/]+$/.test(pathname) ||
                /^\/marketplace\/publishers\/[^/]+$/.test(pathname);
            if (publicMarketplaceRoute && request.method === "GET") {
                return render({ session: null, pendingChallenge: null, accountView: null, catalogPool: service.pool });
            }
            const productPage = /^\/marketplace\/([^/]+)\/([^/]+)$/.exec(pathname);
            if (productPage && !["v1", "publishers"].includes(productPage[1]) &&
                ["GET", "HEAD"].includes(request.method)) {
                const state = await localBrowserRequestState(service.authentication, request);
                const productView = await localProductPage(service.pool, productPage[1], productPage[2], state.session);
                const rendered = await render({ ...state, accountView: null, productView });
                rendered.headers.set("cache-control", "private, no-store");
                rendered.headers.set("vary", "Cookie, Authorization");
                return rendered;
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
