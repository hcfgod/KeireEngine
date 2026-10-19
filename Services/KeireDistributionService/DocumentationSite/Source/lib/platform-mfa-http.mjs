import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { beginLocalAuthenticator, confirmLocalAuthenticator, removeLocalAuthenticator } from "./platform-mfa-management.mjs";

const routes = new Set(["/account/mfa/enroll", "/account/mfa/verify", "/account/mfa/remove"]);
const sessionCookie = (token, seconds) => `__Host-keire-session=${token}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${seconds}`;

export async function handleLocalMfaRequest(auth, request, siteOrigin, clientIdentity, correlationId) {
    const path = new URL(request.url).pathname.replace(/\/+$/, "");
    if (!routes.has(path)) {
        return null;
    }
    const headers = new Headers({ "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store", "vary": "Cookie, Authorization", "x-content-type-options": "nosniff" });
    const respond = (body, status = 200) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== "POST") {
        headers.set("allow", "POST");
        return fail("request.method_not_allowed", "Use POST to manage authenticators.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use browser sign-in for authenticator management.", 401);
    }
    if (new URL(siteOrigin).protocol !== "https:" || new URL(siteOrigin).origin !== siteOrigin || request.headers.get("origin") !== siteOrigin) {
        return fail("request.origin_rejected", "The request origin could not be verified.", 403);
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return fail("account.authentication_required", "Sign in to manage authenticators.", 401);
    }
    let input;
    try {
        input = await readObject(request, 4096);
    } catch {
        return fail("request.invalid_body", "The request is invalid or too large.", 400);
    }
    try {
        const result = path.endsWith("/enroll")
            ? await beginLocalAuthenticator(auth, token, input.password, input.friendlyName, clientIdentity)
            : path.endsWith("/verify")
                ? await confirmLocalAuthenticator(auth, token, input.factorId, input.code)
                : await removeLocalAuthenticator(auth, token, input.factorId);
        if (result.state === "pending") {
            return respond({ data: { factorId: result.factorId, secret: result.secret, uri: result.uri, expiresAt: result.expiresAt } });
        }
        if (result.state === "authenticated") {
            const lifetime = Math.max(0, Math.floor((result.expiresAt - auth.now()) / 1000));
            headers.append("set-cookie", sessionCookie(result.token, lifetime));
            return respond({ data: { currentLevel: "aal2", nextLevel: "aal2" } });
        }
        if (result.state === "removed") {
            if (result.signedOut) {
                headers.append("set-cookie", sessionCookie("", 0));
            }
            return respond({ data: { removed: true, signedOut: result.signedOut } });
        }
        const failures = {
            invalid_session: [401, "Sign in again to manage authenticators."],
            mfa_required: [403, "Verify your existing authenticator before changing authentication methods."],
            rate_limited: [429, "Too many attempts. Wait before trying again."],
            invalid_credentials: [400, "Check your current password and try again."],
            factor_limit: [409, "Remove an unused or unfinished authenticator before adding another."],
            factor_missing: [404, "This authenticator is unavailable."],
            invalid_enrollment: [400, "This setup is expired or already used. Cancel it and start again."],
            invalid_code: [400, "The authenticator code is invalid. Enter its current six-digit code."],
        };
        const [status, message] = failures[result.state] ?? [400, "Check the authenticator setup details."];
        return fail(`account.${result.state}`, message, status);
    } catch {
        return fail("account.mfa_unavailable", "The change could not be confirmed. Reload your security settings before trying again.", 503);
    }
}
