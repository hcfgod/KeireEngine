import { cookieToken } from "./platform-auth-http.mjs";
import { createGitHubProvider } from "./platform-github-provider.mjs";
import { beginGitHubAuthorization, consumeGitHubAuthorization, completeGitHubIdentity, githubNextPath }
    from "./platform-github-authorization.mjs";

const browserCookie = "__Host-keire-github";
const cookie = (name, value, seconds) => `${name}=${value}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${seconds}`;
const escape = (text) => text.replace(/[&<>"']/g, (value) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
})[value]);

export function isGitHubPath(path) {
    return ["/account/github", "/account/github/callback"].includes(path.replace(/\/+$/, ""));
}

export async function handleGitHubRequest(authentication, provider, request, clientIdentity) {
    const url = new URL(request.url);
    if (!isGitHubPath(url.pathname)) {
        return null;
    }
    const headers = new Headers({ "cache-control": "private, no-store", "vary": "Cookie, Authorization",
        "referrer-policy": "no-referrer", "x-content-type-options": "nosniff",
        "content-security-policy": "default-src 'none'; form-action 'self' https://github.com; frame-ancestors 'none'; base-uri 'none'" });
    const page = (message, status = 400, content = "") => {
        headers.set("content-type", "text/html; charset=utf-8");
        return new Response(`<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>GitHub sign-in — Kéire</title><body><main><h1>GitHub sign-in</h1><p>${escape(message)}</p>${content}<p><a href="/account/">Back to account</a> · <a href="/account/sign-in/">Other sign-in methods</a></p></main></body></html>`, { status, headers });
    };
    const redirect = (location) => {
        headers.set("location", location);
        return new Response(null, { status: 303, headers });
    };
    if (request.headers.has("authorization") || url.origin !== provider.siteOrigin) {
        return page("Use your browser and the configured website to sign in.", 403);
    }
    const callback = url.pathname.replace(/\/+$/, "").endsWith("/callback");
    const sessionToken = cookieToken(request, "__Host-keire-session");
    if ((!callback && !["GET", "POST"].includes(request.method)) || (callback && request.method !== "GET")) {
        headers.set("allow", callback ? "GET" : "GET, POST");
        return page("This request method is not supported.", 405);
    }
    try {
        if (!callback) {
            if (url.searchParams.getAll("next").length > 1 || [...url.searchParams.keys()].some((key) => key !== "next")) {
                return page("The sign-in destination is invalid.");
            }
            const next = githubNextPath(url.searchParams.get("next"));
            if (request.method === "GET") {
                const session = sessionToken ? await authentication.session(sessionToken) : null;
                if (sessionToken && !session) {
                    return page("Your session expired. Sign in again before linking GitHub.", 401);
                }
                const label = session ? "Link my GitHub identity" : "Continue with GitHub";
                return page(session ? "Connect GitHub to the account you are currently signed into."
                    : "Continue to GitHub to verify your existing linked account. New users can create an account first, then link GitHub in Security.",
                200, `<form method="post" action="/account/github/?next=${escape(encodeURIComponent(next))}"><button type="submit">${label}</button></form><p><a href="/account/create/">Create an account</a></p>`);
            }
            if (request.headers.get("origin") !== provider.siteOrigin ||
                request.headers.get("sec-fetch-site") === "cross-site") {
                return page("The request origin could not be verified. Return to your account and try again.", 403);
            }
            const started = await beginGitHubAuthorization(authentication, provider, sessionToken, next, clientIdentity);
            if (started.state !== "redirect") {
                return page(started.state === "rate_limited" ? "Too many sign-in attempts. Wait 15 minutes and try again."
                    : "Your session expired. Sign in again.", started.state === "rate_limited" ? 429 : 401);
            }
            headers.append("set-cookie", cookie(browserCookie, started.browser, 600));
            return redirect(started.url);
        }
        headers.append("set-cookie", cookie(browserCookie, "", 0));
        const allowed = new Set(["state", "code", "error", "error_description", "error_uri"]);
        if ([...url.searchParams.keys()].some((key) => !allowed.has(key) || url.searchParams.getAll(key).length !== 1)) {
            return page("This GitHub callback is invalid. Start sign-in again.");
        }
        const authorization = await consumeGitHubAuthorization(authentication, provider, url.searchParams.get("state"),
            cookieToken(request, browserCookie), sessionToken);
        if (!authorization) {
            return page("This sign-in has expired, was already used, or belongs to another browser. Start again.");
        }
        if (url.searchParams.has("error")) {
            return page("GitHub authorization was not completed. You can try again or use another sign-in method.");
        }
        const identity = await provider.identity(url.searchParams.get("code"), authorization.verifier);
        const result = await completeGitHubIdentity(authentication, identity, authorization, sessionToken);
        if (result.state === "linked") {
            return redirect(authorization.next);
        }
        if (result.state === "authenticated" || result.state === "mfa_required") {
            headers.append("set-cookie", cookie("__Host-keire-email-challenge", "", 0));
            headers.append("set-cookie", cookie("__Host-keire-email-enrollment", "", 0));
            headers.append("set-cookie", cookie("__Host-keire-session", result.token ?? "",
                result.state === "authenticated" ? Math.max(0, Math.floor((result.expiresAt - authentication.now()) / 1000)) : 0));
            headers.append("set-cookie", cookie("__Host-keire-challenge", result.challengeToken ?? "",
                result.state === "mfa_required" ? 300 : 0));
            return redirect(result.state === "mfa_required"
                ? `/account/mfa/challenge/?next=${encodeURIComponent(authorization.next)}` : authorization.next);
        }
        if (result.state === "registration_required") {
            return page("This GitHub identity is not linked yet. Sign in to your existing Kéire account and link GitHub in Security, or create an account first.",
                409, '<p><a href="/account/create/">Create an account</a></p>');
        }
        if (result.state === "verification_required") {
            return page("Sign in again using your enrolled authenticator before linking GitHub.", 403);
        }
        if (result.state === "identity_conflict") {
            return page("This GitHub identity is already linked to another account, or your account already has a different GitHub identity. No account was changed.", 409);
        }
        return page("This account could not be signed in or linked. Sign in using another method and check your security settings.", 401);
    } catch {
        return page("GitHub sign-in could not be completed. Start again, verify your GitHub email, or use another sign-in method.", 503);
    }
}

/** Application-owned, injected into the MySQL backend. Shutdown drains accepted requests before releasing its secret. */
export function createLocalGitHubService({ authentication, settings, fetcher }) {
    const provider = createGitHubProvider(settings, fetcher);
    const active = new Set();
    let closing = false;
    let shutdown;
    return {
        handle(request, clientIdentity) {
            if (!isGitHubPath(new URL(request.url).pathname)) {
                return Promise.resolve(null);
            }
            if (closing) {
                return Promise.resolve(new Response("GitHub sign-in is restarting. Please try again.", {
                    status: 503, headers: { "cache-control": "private, no-store" },
                }));
            }
            const operation = handleGitHubRequest(authentication, provider, request, clientIdentity);
            active.add(operation);
            operation.finally(() => active.delete(operation)).catch(() => {});
            return operation;
        },
        close() {
            if (!shutdown) {
                closing = true;
                shutdown = Promise.allSettled([...active]).then(() => provider.close());
            }
            return shutdown;
        },
    };
}
