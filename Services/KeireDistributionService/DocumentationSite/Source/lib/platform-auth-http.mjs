const sessionCookie = "__Host-keire-session";
const challengeCookie = "__Host-keire-challenge";
const tokenPattern = /^[A-Za-z0-9_-]{43}$/;

function cookieToken(request, name) {
    const values = (request.headers.get("cookie") ?? "").split(";")
        .map((part) => part.trim()).filter((part) => part.startsWith(`${name}=`));
    if (values.length !== 1) {
        return null;
    }
    const value = values[0].slice(name.length + 1);
    return tokenPattern.test(value) ? value : null;
}

function cookie(name, value, seconds) {
    return `${name}=${value}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${seconds}`;
}

async function readObject(request) {
    if (request.headers.get("content-type")?.split(";")[0].trim().toLowerCase() !== "application/json") {
        throw new Error("invalid body");
    }
    const reader = request.body?.getReader();
    if (!reader) {
        throw new Error("invalid body");
    }
    const chunks = [];
    let length = 0;
    try {
        while (true) {
            const result = await reader.read();
            if (result.done) {
                break;
            }
            length += result.value.byteLength;
            if (length > 8192) {
                await reader.cancel();
                throw new Error("invalid body");
            }
            chunks.push(result.value);
        }
    } finally {
        reader.releaseLock();
    }
    const value = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(Buffer.concat(chunks)));
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new Error("invalid body");
    }
    return value;
}

/** Application-owned adapter. clientIdentity must be supplied by trusted server/proxy code. */
export function createLocalAuthenticationHttp(authentication, siteOrigin) {
    const configured = new URL(siteOrigin);
    if (configured.protocol !== "https:" || configured.origin !== siteOrigin) {
        throw new Error("Authentication requires a canonical HTTPS origin.");
    }
    return async function handle(request, clientIdentity, correlationId) {
        const headers = new Headers({
            "content-type": "application/json; charset=utf-8",
            "cache-control": "private, no-store",
            "vary": "Cookie, Authorization",
            "x-content-type-options": "nosniff",
        });
        const respond = (body, status = 200) => new Response(JSON.stringify(body), { status, headers });
        const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
        const location = new URL(request.url).pathname.replace(/\/+$/, "");
        const sessionPath = location === "/account/session";
        const verifyPath = location === "/account/mfa/challenge/verify";
        const deletePath = location === "/account/session/delete";
        const challengeStatusPath = location === "/account/mfa/challenge/status";
        if (!sessionPath && !verifyPath && !deletePath && !challengeStatusPath) {
            return null;
        }
        const mutation = request.method === "POST";
        if ((challengeStatusPath && request.method !== "GET") ||
            (!mutation && !((sessionPath || challengeStatusPath) && request.method === "GET"))) {
            headers.set("allow", challengeStatusPath ? "GET" : sessionPath ? "GET, POST" : "POST");
            return fail("request.method_not_allowed", "This method is not supported.", 405);
        }
        // Browser authentication is cookie-only; Hub bearer/OAuth uses a separate adapter.
        if (request.headers.has("authorization")) {
            return fail("account.invalid_authorization", "Use browser sign-in for this endpoint.", 401);
        }
        if (mutation && request.headers.get("origin") !== siteOrigin) {
            return fail("request.origin_rejected", "The request origin could not be verified.", 403);
        }
        if (mutation && (typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 256)) {
            return fail("account.service_unavailable", "Sign-in is temporarily unavailable. Try again.", 503);
        }
        try {
            if (challengeStatusPath) {
                const challenge = await authentication.pendingChallenge(cookieToken(request, challengeCookie));
                if (!challenge) {
                    headers.append("set-cookie", cookie(challengeCookie, "", 0));
                    return fail("account.challenge_expired", "Sign in again to start a new verification challenge.", 401);
                }
                return respond({ data: challenge });
            }
            const token = cookieToken(request, sessionCookie);
            if (!mutation) {
                const session = token ? await authentication.session(token) : null;
                return respond({ data: { signedIn: Boolean(session),
                    assuranceLevel: session ? `aal${session.assurance}` : null } });
            }
            if (deletePath) {
                if (token) {
                    await authentication.signOut(token);
                }
                headers.append("set-cookie", cookie(sessionCookie, "", 0));
                headers.append("set-cookie", cookie(challengeCookie, "", 0));
                headers.set("location", "/");
                return new Response(null, { status: 303, headers });
            }
            let input;
            try {
                input = await readObject(request);
            } catch {
                return fail("request.invalid_body", "Send a valid JSON object of at most 8 KiB.", 400);
            }
            const result = sessionPath
                ? await authentication.signIn(input.email, input.password, clientIdentity)
                : await authentication.completeTotp(cookieToken(request, challengeCookie), input.factorId, input.code);
            if (result.state === "authenticated") {
                if (token) {
                    await authentication.signOut(token);
                }
                headers.append("set-cookie", cookie(sessionCookie, result.token, 8 * 60 * 60));
                headers.append("set-cookie", cookie(challengeCookie, "", 0));
                return respond({ data: { userId: result.userId, mfaRequired: false } });
            }
            if (result.state === "mfa_required") {
                if (token) {
                    await authentication.signOut(token);
                }
                headers.append("set-cookie", cookie(sessionCookie, "", 0));
                // The challenge is not a signed-in session and is never exposed to JavaScript.
                headers.append("set-cookie", cookie(challengeCookie, result.challengeToken, 5 * 60));
                return respond({ data: { mfaRequired: true, factors: result.factors } });
            }
            if (result.state === "rate_limited") {
                headers.set("retry-after", "900");
                return fail("account.rate_limited", "Too many sign-in attempts. Try again in 15 minutes.", 429);
            }
            if (result.state === "unsupported_mfa") {
                return fail("account.mfa_unavailable", "This account’s verification method is not available yet.", 503);
            }
            return fail("account.invalid_credentials", "The credentials or verification code are incorrect or expired.", 401);
        } catch {
            return fail("account.service_unavailable", "Sign-in is temporarily unavailable. Try again.", 503);
        }
    };
}
