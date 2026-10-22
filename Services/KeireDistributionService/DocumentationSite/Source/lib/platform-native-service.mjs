import { createNativeIdentitySigner } from "./platform-native-identity.mjs";
import { createNativeOAuthPolicy } from "./platform-native-oauth-policy.mjs";
import { createNativeTokenHttp } from "./platform-native-token-http.mjs";
import { createNativeBrowserHttp } from "./platform-native-browser.mjs";
import { createNativeAccountHttp } from "./platform-native-account-http.mjs";
import { createNativeMarketplaceHttp } from "./platform-native-marketplace-http.mjs";
import { createDownloadCapabilities } from "./platform-download-capabilities.mjs";
import { createNativeDownloadHttp } from "./platform-native-download-http.mjs";

export function isNativeAuthorizationPath(pathname) {
    return pathname.startsWith("/auth/v1/oauth/") || pathname.startsWith("/auth/v1/.well-known/")
        || pathname.startsWith("/marketplace/v1/downloads/")
        || ["/oauth/consent", "/oauth/decision", "/oauth/hub/callback", "/auth/v1/user", "/auth/v1/logout", "/rest/v1/profiles",
            "/functions/v1/marketplace-hub"]
            .includes(pathname.replace(/\/+$/, ""));
}

/** Owns the identity signer; borrows the host's database and browser authentication service. */
export function createLocalNativeService({ pool, authentication, siteOrigin, clientId, privateKey, downloadKey, packageStore }) {
    const policy = createNativeOAuthPolicy({ siteOrigin, clientId });
    const signer = createNativeIdentitySigner({ siteOrigin, clientId, privateKey });
    let token;
    let browser;
    let account;
    let marketplace;
    let capabilities;
    let downloads;
    try {
        if (downloadKey !== undefined || packageStore !== undefined) {
            if (!packageStore || typeof packageStore.read !== "function" || typeof packageStore.verify !== "function") {
                throw new Error("Native downloads require a verified private package store.");
            }
            capabilities = createDownloadCapabilities({ siteOrigin, key: downloadKey });
            downloads = createNativeDownloadHttp({ pool, clientId, capabilities, packageStore });
        }
        token = createNativeTokenHttp({ pool, policy, signer });
        browser = createNativeBrowserHttp({ pool, authentication, policy, siteOrigin });
        account = createNativeAccountHttp({ pool, siteOrigin, clientId });
        marketplace = createNativeMarketplaceHttp({ pool, siteOrigin, clientId, capabilities, packageStore });
    } catch (error) {
        capabilities?.close();
        signer.close();
        throw error;
    }
    let closing = false;
    let active = 0;
    let drained;
    let shutdown;
    return Object.freeze({
        async handle(request, clientIdentity, render) {
            const url = new URL(request.url);
            if (!isNativeAuthorizationPath(url.pathname)) {
                return null;
            }
            if (closing) {
                return new Response("Desktop authorization is temporarily unavailable.", {
                    status: 503, headers: { "cache-control": "private, no-store" },
                });
            }
            active++;
            try {
                if (url.pathname.startsWith("/marketplace/v1/downloads/")) {
                    return downloads ? await downloads(request) : new Response(null, {
                        status: 503, headers: { "cache-control": "private, no-store" },
                    });
                }
                if (url.pathname.startsWith("/auth/v1/.well-known/")) {
                    const headers = { "cache-control": "public, max-age=300", "x-content-type-options": "nosniff" };
                    if (url.origin !== siteOrigin || url.search) {
                        return new Response(null, { status: 400 });
                    }
                    if (request.method !== "GET") {
                        return new Response(null, { status: 405, headers: { allow: "GET" } });
                    }
                    if (url.pathname === "/auth/v1/.well-known/jwks.json") {
                        return Response.json(signer.jwks, { headers });
                    }
                    if (url.pathname === "/auth/v1/.well-known/openid-configuration") {
                        return Response.json({
                            issuer: signer.issuer, authorization_endpoint: `${siteOrigin}/auth/v1/oauth/authorize`,
                            token_endpoint: `${siteOrigin}/auth/v1/oauth/token`, jwks_uri: `${signer.issuer}/.well-known/jwks.json`,
                            response_types_supported: ["code"], subject_types_supported: ["public"],
                            id_token_signing_alg_values_supported: ["EdDSA"], scopes_supported: ["openid", "email", "profile"],
                            token_endpoint_auth_methods_supported: ["none"], grant_types_supported: ["authorization_code", "refresh_token"],
                            code_challenge_methods_supported: ["S256"],
                        }, { headers });
                    }
                    return new Response(null, { status: 404 });
                }
                return await token(request, clientIdentity) ?? await account(request)
                    ?? await marketplace(request)
                    ?? await browser(request, clientIdentity, render)
                    ?? new Response(null, { status: 404, headers: { "cache-control": "private, no-store" } });
            } finally {
                active--;
                if (active === 0) {
                    drained?.();
                }
            }
        },
        close() {
            closing = true;
            shutdown ??= (async () => {
                if (active !== 0) {
                    await new Promise((resolve) => { drained = resolve; });
                }
                try {
                    await downloads?.close();
                } finally {
                    capabilities?.close();
                    signer.close();
                }
            })();
            return shutdown;
        },
    });
}
