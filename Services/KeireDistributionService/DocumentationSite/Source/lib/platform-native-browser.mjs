import { cookieToken, localBrowserRequestState } from "./platform-auth-http.mjs";
import { tokenDigest } from "./platform-auth-crypto.mjs";
import { createNativeAuthorization, decideNativeAuthorization } from "./platform-native-authorization.mjs";
import { reserveNativeRequest } from "./platform-native-limits.mjs";
import { NativeOAuthError } from "./platform-native-oauth-policy.mjs";
import { readNativeForm } from "./platform-native-token-http.mjs";

export function createNativeBrowserHttp({ pool, authentication, policy, siteOrigin }) {
    if (new URL(policy.redirectUri).origin !== siteOrigin || typeof authentication?.session !== "function"
        || typeof authentication?.withSessionTransaction !== "function") {
        throw new Error("Native consent requires matching browser authentication and a registered callback.");
    }
    return async function handle(request, clientIdentity, render) {
        const url = new URL(request.url);
        const pathname = url.pathname.replace(/\/+$/, "");
        if (!["/auth/v1/oauth/authorize", "/oauth/consent", "/oauth/decision", "/oauth/hub/callback"].includes(pathname)) {
            return null;
        }
        const headers = new Headers({ "cache-control": "private, no-store", "referrer-policy": "no-referrer",
            "x-content-type-options": "nosniff", vary: "Cookie, Authorization" });
        const redirect = (location) => new Response(null, { status: 303, headers: { ...Object.fromEntries(headers), location } });
        const fail = (message, status) => new Response(message, { status, headers });
        try {
            if (url.origin !== siteOrigin || request.headers.has("authorization") || request.headers.has("apikey")) {
                return fail("Use the website sign-in page to authorize Kéire Hub.", 403);
            }
            const expectedMethod = pathname === "/oauth/decision" ? "POST" : "GET";
            if (request.method !== expectedMethod) {
                headers.set("allow", expectedMethod);
                return fail(`Use ${expectedMethod} for this authorization step.`, 405);
            }
            if (pathname === "/auth/v1/oauth/authorize") {
                await reserveNativeRequest(pool, clientIdentity, "authorize");
                const created = await createNativeAuthorization(pool, policy, url.searchParams);
                return redirect(`/oauth/consent/?authorization_id=${encodeURIComponent(created.requestToken)}`);
            }
            if (pathname === "/oauth/decision") {
                if (request.headers.get("origin") !== siteOrigin || url.search) {
                    return fail("Return to the authorization page and submit your decision there.", 403);
                }
                const parameters = await readNativeForm(request);
                const requestToken = parameters.get("authorization_id");
                const decision = parameters.get("decision");
                if (parameters.size !== 2 || parameters.getAll("authorization_id").length !== 1
                    || parameters.getAll("decision").length !== 1 || !["approve", "deny"].includes(decision)) {
                    return fail("The authorization decision is invalid. Start sign-in again from Kéire Hub.", 400);
                }
                const result = await decideNativeAuthorization(authentication,
                    cookieToken(request, "__Host-keire-session"), policy, requestToken, decision === "approve");
                return redirect(result.redirectUrl);
            }
            if (pathname === "/oauth/hub/callback") {
                const response = await render({ session: null, pendingChallenge: null, accountView: null });
                for (const [name, value] of headers) {
                    response.headers.set(name, value);
                }
                return response;
            }
            const requestToken = url.searchParams.get("authorization_id");
            const hash = tokenDigest(requestToken);
            if (!hash || url.searchParams.size !== 1) {
                return fail("This authorization link is invalid. Start sign-in again from Kéire Hub.", 400);
            }
            const state = await localBrowserRequestState(authentication, request);
            if (!state.session) {
                const next = `/oauth/consent/?authorization_id=${encodeURIComponent(requestToken)}`;
                const destination = state.pendingChallenge ? "/account/mfa/challenge/" : "/account/sign-in/";
                return redirect(`${destination}?next=${encodeURIComponent(next)}`);
            }
            const [[authorization]] = await pool.execute(`SELECT client_id,redirect_uri,scope FROM platform_native_authorizations
                WHERE request_hash=? AND decision='pending' AND expires_ms>?`, [hash, Date.now()]);
            if (!authorization || authorization.client_id !== policy.clientId || authorization.redirect_uri !== policy.redirectUri) {
                return fail("This authorization link expired or was already used. Start sign-in again from Kéire Hub.", 400);
            }
            const response = await render({ ...state, accountView: null, nativeConsent: {
                authorization_id: requestToken, client: { name: "Kéire Hub" },
                redirect_uri: policy.redirectUri, scope: authorization.scope,
            } });
            for (const [name, value] of headers) {
                response.headers.set(name, value);
            }
            return response;
        } catch (error) {
            if (error instanceof NativeOAuthError) {
                if (error.status === 429) {
                    headers.set("retry-after", "600");
                }
                return fail(`${error.message} Start sign-in again from Kéire Hub if needed.`, error.status);
            }
            return fail("Desktop authorization is temporarily unavailable. Please try again shortly.", 503);
        }
    };
}
