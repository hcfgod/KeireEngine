import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { enqueueRegistration } from "./platform-registration.mjs";
import { confirmRegistration, registrationStatus } from "./platform-registration-confirmation.mjs";

const registrationCookie = "__Host-keire-registration";
const routes = new Map([
    ["/account/registration", "POST"],
    ["/account/registration/session", "POST"],
    ["/account/registration/status", "GET"],
    ["/account/registration/confirm", "POST"],
]);
const cookie = (token, seconds) =>
    `${registrationCookie}=${token}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${seconds}`;

export async function handleLocalRegistrationRequest(service, request, siteOrigin, clientIdentity, correlationId) {
    const pathname = new URL(request.url).pathname.replace(/\/+$/, "");
    const method = routes.get(pathname);
    if (!method) {
        return null;
    }
    const headers = new Headers({
        "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store",
        "vary": "Cookie, Authorization",
        "x-content-type-options": "nosniff",
        "referrer-policy": "no-referrer",
    });
    const respond = (body, status = 200) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== method) {
        headers.set("allow", method);
        return fail("request.method_not_allowed", "This method is not supported.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use the browser registration flow.", 401);
    }
    if (new URL(siteOrigin).protocol !== "https:" || new URL(siteOrigin).origin !== siteOrigin ||
        (method !== "GET" && request.headers.get("origin") !== siteOrigin)) {
        return fail("request.origin_rejected", "The request origin could not be verified.", 403);
    }
    if (!service.registrationWorker || typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 200) {
        return fail("account.registration_unavailable", "Account registration is temporarily unavailable. Try again later.", 503);
    }
    let input = {};
    if (method !== "GET") {
        try {
            input = await readObject(request, 8192);
        } catch {
            return fail("request.invalid_body", "The registration request is invalid or too large.", 400);
        }
    }
    try {
        const auth = service.authentication;
        if (pathname === "/account/registration") {
            const result = await enqueueRegistration(auth, input, clientIdentity);
            if (result.state === "invalid_request") {
                return fail("account.invalid_registration",
                    "Enter a valid email and display name, accept the terms, and use a password of at least 10 characters and at most 72 UTF-8 bytes.", 400);
            }
            return respond({ data: { verificationRequired: true } }, 202);
        }
        if (pathname === "/account/registration/session") {
            const status = await registrationStatus(auth, input.token);
            if (!status) {
                headers.append("set-cookie", cookie("", 0));
                return fail("account.invalid_confirmation", "This confirmation link is invalid or expired. Start registration again.", 400);
            }
            headers.append("set-cookie", cookie(input.token, Math.max(0, Math.min(1800,
                Math.floor((status.expiresAt - auth.now()) / 1000)))));
            return respond({ data: status });
        }
        const token = cookieToken(request, registrationCookie);
        if (pathname === "/account/registration/status") {
            const status = await registrationStatus(auth, token);
            return status ? respond({ data: status })
                : fail("account.invalid_confirmation", "This confirmation link is invalid or expired. Start registration again.", 400);
        }
        const result = await confirmRegistration(auth, token, input.password, clientIdentity);
        if (result.state === "rate_limited") {
            return fail("account.rate_limited", "Too many attempts. Wait before trying again.", 429);
        }
        if (result.state !== "confirmed") {
            return fail("account.invalid_confirmation",
                "Use the password you chose when registering. If this link is expired or already used, sign in or start registration again.", 400);
        }
        headers.append("set-cookie", cookie("", 0));
        return respond({ data: { confirmed: true } });
    } catch {
        return fail("account.registration_unavailable",
            "Registration could not be confirmed. Check your inbox or try signing in before registering again.", 503);
    }
}
