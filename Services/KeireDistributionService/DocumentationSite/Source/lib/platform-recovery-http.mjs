import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { enqueueRecoveryDelivery } from "./platform-recovery-delivery.mjs";
import { recoveryStatus } from "./platform-recovery-store.mjs";
import { replaceLocalRecoveredPassword } from "./platform-password-recovery.mjs";

const recoveryCookie = "__Host-keire-recovery";
const routes = new Map([
    ["/account/recovery", "POST"],
    ["/account/recovery/session", "POST"],
    ["/account/recovery/status", "GET"],
    ["/account/password", "PATCH"],
]);
const cookie = (name, token, lifetime) => `${name}=${token}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${lifetime}`;

export async function handleLocalRecoveryRequest(service, request, siteOrigin, clientIdentity, correlationId) {
    const pathname = new URL(request.url).pathname.replace(/\/+$/, "");
    const method = routes.get(pathname);
    if (!method) {
        return null;
    }
    const headers = new Headers({ "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store", "vary": "Cookie, Authorization", "x-content-type-options": "nosniff" });
    const respond = (body, status = 200) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== method) {
        headers.set("allow", method);
        return fail("request.method_not_allowed", "This method is not supported.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use the browser recovery flow.", 401);
    }
    if (new URL(siteOrigin).protocol !== "https:" || new URL(siteOrigin).origin !== siteOrigin ||
        (method !== "GET" && request.headers.get("origin") !== siteOrigin)) {
        return fail("request.origin_rejected", "The request origin could not be verified.", 403);
    }
    if (!service.recoveryWorker || typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 200) {
        return fail("account.recovery_unavailable", "Password recovery is temporarily unavailable. Try again later.", 503);
    }
    let input = {};
    if (method !== "GET") {
        try {
            input = await readObject(request);
        } catch {
            return fail("request.invalid_body", "The recovery request is invalid or too large.", 400);
        }
    }
    try {
        const auth = service.authentication;
        if (pathname === "/account/recovery") {
            const result = await enqueueRecoveryDelivery(auth, input.email, clientIdentity);
            if (result.state === "invalid_request") {
                return fail("request.invalid_email", "Enter a valid email address.", 400);
            }
            return respond({ message: "If the account exists, a recovery email will be sent." }, 202);
        }
        if (pathname === "/account/recovery/session") {
            const status = await recoveryStatus(auth, input.token);
            if (!status) {
                return fail("account.invalid_recovery", "This recovery link is invalid or expired. Request a new one.", 400);
            }
            const lifetime = Math.max(0, Math.min(900, Math.floor((status.expiresAt - auth.now()) / 1000)));
            headers.append("set-cookie", cookie(recoveryCookie, input.token, lifetime));
            return respond({ data: status });
        }
        const token = cookieToken(request, recoveryCookie);
        if (pathname === "/account/recovery/status") {
            const status = await recoveryStatus(auth, token);
            return status ? respond({ data: status })
                : fail("account.invalid_recovery", "This recovery link is invalid or expired. Request a new one.", 400);
        }
        const result = await replaceLocalRecoveredPassword(auth, token, input.password, input.factorId, input.code, clientIdentity);
        if (result.state === "invalid_password") {
            return fail("account.password_too_weak", "Use at least 10 characters and no more than 72 UTF-8 bytes.", 400);
        }
        if (result.state === "rate_limited") {
            return fail("account.rate_limited", "Too many attempts. Wait before trying again.", 429);
        }
        if (result.state !== "updated") {
            return fail("account.invalid_recovery", "Check your authenticator code and choose a different password. If the link expired, request a new one.", 400);
        }
        headers.append("set-cookie", cookie(recoveryCookie, "", 0));
        headers.append("set-cookie", cookie("__Host-keire-session", "", 0));
        return respond({ data: { updated: true, signedOut: true } });
    } catch {
        return fail("account.recovery_unavailable", "Password recovery could not be completed. Try again later.", 503);
    }
}
