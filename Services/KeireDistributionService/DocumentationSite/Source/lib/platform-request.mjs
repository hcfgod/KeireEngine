import { cookieToken, createLocalAuthenticationHttp, localBrowserRequestState } from "./platform-auth-http.mjs";
import { localPublisherPortal } from "./platform-publisher-portal.mjs";
import { localAccountOverview } from "./platform-account-view.mjs";
import { localProductPage } from "./platform-product.mjs";
import { handleLocalClaimRequest } from "./platform-claims-http.mjs";
import { handleLocalRecoveryRequest } from "./platform-recovery-http.mjs";
import { handleLocalOrganizationRequest } from "./platform-organizations-http.mjs";
import { handleLocalDataRequest } from "./platform-data-http.mjs";
import { handleLocalMfaRequest } from "./platform-mfa-http.mjs";
import { handleLocalRegistrationRequest } from "./platform-registration-http.mjs";
import { handleLocalPublisherApplicationRequest } from "./platform-publisher-applications-http.mjs";
import { handleLocalUploadStatusRequest } from "./platform-upload-status-http.mjs";
import { handleLocalUploadRequest } from "./platform-uploads-http.mjs";
import { handleLocalVersionSubmit } from "./platform-version-submit-http.mjs";
import { handleLocalReviewRequest } from "./platform-review-http.mjs";
import { isNativeAuthorizationPath } from "./platform-native-service.mjs";

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
            if (isNativeAuthorizationPath(new URL(request.url).pathname)) {
                if (!service.native) {
                    return unavailable("Desktop sign-in is not configured. Please try again later.");
                }
                return await service.native.handle(request, clientIdentity, render) ?? new Response(null, {
                    status: 404, headers: { "cache-control": "private, no-store" },
                });
            }
            if (new URL(request.url).pathname.startsWith("/internal/publication/")) {
                if (!service.publication) {
                    return unavailable("The publication service is not configured.");
                }
                return await service.publication.handle(request) ?? new Response(null, {
                    status: 404, headers: { "cache-control": "private, no-store" },
                });
            }
            if (new URL(request.url).pathname.startsWith("/internal/validator/")) {
                if (!service.validator) {
                    return unavailable("The validator service is not configured.");
                }
                const response = await service.validator.handle(request);
                return response ?? new Response(null, {
                    status: 404, headers: { "cache-control": "private, no-store" },
                });
            }
            const reviewResponse = await handleLocalReviewRequest(service.authentication, request, siteOrigin, correlationId);
            if (reviewResponse) {
                return reviewResponse;
            }
            const submissionResponse = await handleLocalVersionSubmit(service.authentication, request, siteOrigin, correlationId);
            if (submissionResponse) {
                return submissionResponse;
            }
            const uploadResponse = await handleLocalUploadRequest(service, request, siteOrigin, correlationId);
            if (uploadResponse) {
                return uploadResponse;
            }
            const registrationResponse = await handleLocalRegistrationRequest(service, request, siteOrigin, clientIdentity, correlationId);
            if (registrationResponse) {
                return registrationResponse;
            }
            const mfaResponse = await handleLocalMfaRequest(service.authentication, request, siteOrigin, clientIdentity, correlationId);
            if (mfaResponse) {
                return mfaResponse;
            }
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
            const uploadStatus = /^\/publisher\/v1\/uploads\/([^/]+)$/.exec(pathname);
            if (uploadStatus && ["GET", "DELETE"].includes(request.method)) {
                return handleLocalUploadStatusRequest(service.authentication, request, uploadStatus[1], correlationId, siteOrigin);
            }
            if (pathname === "/publisher/v1/applications") {
                return handleLocalPublisherApplicationRequest(service.authentication, request, siteOrigin, correlationId);
            }
            if (pathname === "/publisher" && ["GET", "HEAD"].includes(request.method)) {
                const state = await localBrowserRequestState(service.authentication, request);
                const headers = { "cache-control": "private, no-store", "vary": "Cookie, Authorization" };
                if (!state.session || state.session.assurance !== 2) {
                    return new Response(null, { status: 303, headers: { ...headers,
                        location: state.session ? "/account/mfa/challenge/?next=%2Fpublisher%2F"
                            : "/account/sign-in/?next=%2Fpublisher%2F" } });
                }
                const publisherView = await localPublisherPortal(service.authentication,
                    cookieToken(request, "__Host-keire-session"));
                publisherView.uploadsAvailable = Boolean(service.uploads);
                const accountView = await localAccountOverview(service.pool, state.session);
                const rendered = await render({ ...state, accountView, publisherView });
                for (const [name, value] of Object.entries(headers)) {
                    rendered.headers.set(name, value);
                }
                return rendered;
            }
            if (["/account/create", "/account/confirm"].includes(pathname) && ["GET", "HEAD"].includes(request.method)) {
                const rendered = await render({ session: null, pendingChallenge: null, accountView: null,
                    localRegistration: true });
                rendered.headers.set("cache-control", "private, no-store");
                rendered.headers.set("referrer-policy", "no-referrer");
                rendered.headers.set("vary", "Cookie, Authorization");
                return rendered;
            }
            if (pathname === "/account/data/request") {
                return handleLocalDataRequest(service.authentication, request, siteOrigin, correlationId);
            }
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
            if (!["/account", "/account/data", "/account/mfa", "/account/sign-in", "/account/mfa/challenge"].includes(pathname) ||
                !["GET", "HEAD"].includes(request.method)) {
                return unavailable("This platform route is awaiting local-backend migration. Please try again later.");
            }
            const state = await localBrowserRequestState(service.authentication, request);
            const accountView = ["/account", "/account/data", "/account/mfa"].includes(pathname) && state.session
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
