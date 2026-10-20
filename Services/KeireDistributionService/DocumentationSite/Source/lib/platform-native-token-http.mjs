import { exchangeNativeAuthorization } from "./platform-native-authorization.mjs";
import { issueNativeSession, refreshNativeSession } from "./platform-native-sessions.mjs";
import { reserveNativeRequest } from "./platform-native-limits.mjs";
import { NativeOAuthError } from "./platform-native-oauth-policy.mjs";

export async function readNativeForm(request) {
    if (request.headers.get("content-type")?.split(";")[0].trim().toLowerCase() !== "application/x-www-form-urlencoded"
        || !request.body) {
        throw new NativeOAuthError("invalid_request", "Use a form-encoded desktop token request.");
    }
    const reader = request.body.getReader();
    const chunks = [];
    let length = 0;
    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) {
                break;
            }
            length += value.byteLength;
            if (length > 8192) {
                await reader.cancel();
                throw new NativeOAuthError("invalid_request", "The desktop token request is too large.");
            }
            chunks.push(value);
        }
    } finally {
        reader.releaseLock();
    }
    let parameters;
    try {
        parameters = new URLSearchParams(new TextDecoder("utf-8", { fatal: true }).decode(Buffer.concat(chunks)));
    } catch {
        throw new NativeOAuthError("invalid_request", "The desktop token request is malformed.");
    }
    return parameters;
}

async function readTokenForm(request) {
    const parameters = await readNativeForm(request);
    const grantType = parameters.get("grant_type");
    const required = grantType === "authorization_code"
        ? ["grant_type", "client_id", "code", "redirect_uri", "code_verifier"]
        : grantType === "refresh_token" ? ["grant_type", "client_id", "refresh_token"] : null;
    if (!required) {
        throw new NativeOAuthError("unsupported_grant_type", "This desktop authorization grant is unsupported.");
    }
    if (parameters.size !== required.length || required.some((key) => parameters.getAll(key).length !== 1
        || !parameters.get(key))) {
        throw new NativeOAuthError("invalid_request", "The desktop token request has missing or duplicate fields.");
    }
    return Object.fromEntries(parameters);
}

/** The host owns signer and pool lifetime and supplies a trusted clientIdentity for rate limiting. */
export function createNativeTokenHttp({ pool, policy, signer }) {
    const siteOrigin = new URL(policy.redirectUri).origin;
    if (signer.clientId !== policy.clientId || signer.issuer !== `${siteOrigin}/auth/v1`
        || typeof pool?.getConnection !== "function" || typeof signer?.sign !== "function") {
        throw new Error("Native token processing requires a matching registered issuer and database.");
    }
    return async function handle(request, clientIdentity) {
        const url = new URL(request.url);
        if (!["/auth/v1/oauth/token", "/auth/v1/oauth/token/"].includes(url.pathname)) {
            return null;
        }
        const headers = new Headers({
            "content-type": "application/json; charset=utf-8",
            "cache-control": "private, no-store", pragma: "no-cache", "x-content-type-options": "nosniff",
        });
        const respond = (body, status = 200) => new Response(JSON.stringify(body), { status, headers });
        if (request.method !== "POST") {
            headers.set("allow", "POST");
            return respond({ error: "invalid_request", error_description: "Use POST for desktop token requests." }, 405);
        }
        if (url.origin !== siteOrigin || url.search
            || ["origin", "cookie", "authorization", "apikey"].some((name) => request.headers.has(name))) {
            return respond({ error: "invalid_request",
                error_description: "Use the desktop authorization connection without browser credentials." }, 403);
        }
        try {
            await reserveNativeRequest(pool, clientIdentity, "token");
            const input = await readTokenForm(request);
            if (input.grant_type === "refresh_token") {
                return respond(await refreshNativeSession(pool, signer, input.client_id, input.refresh_token));
            }
            return respond(await exchangeNativeAuthorization(pool, policy, {
                clientId: input.client_id, redirectUri: input.redirect_uri,
                code: input.code, verifier: input.code_verifier,
            }, (connection, context) => issueNativeSession(connection, signer, context)));
        } catch (error) {
            if (error instanceof NativeOAuthError) {
                if (error.status === 429) {
                    headers.set("retry-after", "60");
                }
                return respond({ error: error.code, error_description: error.message }, error.status);
            }
            return respond({ error: "temporarily_unavailable",
                error_description: "Desktop sign-in is temporarily unavailable. Please try again." }, 503);
        }
    };
}
