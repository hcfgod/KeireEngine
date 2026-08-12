import { createServerClient, parseCookieHeader } from "@supabase/ssr";
import { createClient } from "@supabase/supabase-js";
import { defineMiddleware } from "astro:middleware";
import { getAssuranceState } from "./lib/auth";
import { runtimeEnvironment } from "./lib/runtime-env";

const mutationMethods = new Set(["POST", "PUT", "PATCH", "DELETE"]);

function isSameOrigin(request: Request): boolean {
    const origin = request.headers.get("origin");
    if (!origin) {
        return false;
    }
    try {
        const requestUrl = new URL(request.url);
        const configuredSiteUrl = runtimeEnvironment("PUBLIC_SITE_URL");
        const forwardedProtocol = request.headers.get("x-forwarded-proto");
        const forwardedHost = request.headers.get("x-forwarded-host");
        if (forwardedProtocol && forwardedProtocol !== "http" && forwardedProtocol !== "https") return false;
        if (forwardedHost && (!/^[A-Za-z0-9.-]+(?::[0-9]{1,5})?$/.test(forwardedHost) || forwardedHost.includes(".."))) {
            return false;
        }
        let expectedOrigin: string;
        if (configuredSiteUrl) {
            const configuredUrl = new URL(configuredSiteUrl);
            if (configuredUrl.protocol !== "https:" || configuredUrl.username || configuredUrl.password ||
                configuredUrl.pathname !== "/" || configuredUrl.search || configuredUrl.hash) {
                return false;
            }
            expectedOrigin = configuredUrl.origin;
        } else {
            const proxyOrigin = forwardedProtocol || forwardedHost
                ? `${forwardedProtocol ?? requestUrl.protocol.slice(0, -1)}://${forwardedHost ?? requestUrl.host}`
                : requestUrl.origin;
            expectedOrigin = new URL(proxyOrigin).origin;
        }
        return new URL(origin).origin === expectedOrigin;
    } catch {
        return false;
    }
}

function jwtStringClaim(token: string, claim: string): string | null {
    try {
        const payload = token.split(".");
        if (payload.length !== 3 || payload[1].length > 16 * 1024) return null;
        const decoded = JSON.parse(Buffer.from(payload[1], "base64url").toString("utf8"));
        return typeof decoded?.[claim] === "string" ? decoded[claim] : null;
    } catch {
        return null;
    }
}

export const onRequest = defineMiddleware(async (context, next) => {
    const authResponseHeaders = new Headers();
    context.locals.correlationId = crypto.randomUUID();
    context.locals.assurance = { currentLevel: null, nextLevel: null };
    context.locals.user = null;
    if (context.request.method === "HEAD" && context.url.pathname.replace(/\/+$/, "") === "/health") {
        return new Response(null, {
            status: 204,
            headers: {
                "cache-control": "no-store",
                "x-content-type-options": "nosniff",
                "x-correlation-id": context.locals.correlationId,
            },
        });
    }
    const supabaseUrl = runtimeEnvironment("PUBLIC_SUPABASE_URL");
    const publishableKey = runtimeEnvironment("PUBLIC_SUPABASE_PUBLISHABLE_KEY");
    const authorization = context.isPrerendered ? null : context.request.headers.get("authorization");
    const bearerMatch = authorization?.match(/^Bearer ([\x21-\x7e]{16,16384})$/i);
    if (authorization && !bearerMatch) {
        return new Response(JSON.stringify({
            error: {
                code: "account.invalid_authorization",
                message: "The authorization header is invalid.",
                correlationId: context.locals.correlationId,
            },
        }), {
            status: 401,
            headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
        });
    }
    const bearerToken = bearerMatch?.[1] ?? null;
    context.locals.supabase = supabaseUrl && publishableKey && bearerToken
        ? createClient(supabaseUrl, publishableKey, {
            auth: { persistSession: false, autoRefreshToken: false, detectSessionInUrl: false },
            global: { headers: { Authorization: `Bearer ${bearerToken}` } },
        })
        : supabaseUrl && publishableKey
        ? createServerClient(supabaseUrl, publishableKey, {
            auth: {
                experimental: { appendPkceFlowIdToRedirects: true },
            },
            cookies: {
                getAll: () => parseCookieHeader(context.request.headers.get("cookie") ?? ""),
                setAll: (cookies, headers) => {
                    for (const { name, value, options } of cookies) {
                        context.cookies.set(name, value, {
                            ...options,
                            path: "/",
                            httpOnly: true,
                            secure: import.meta.env.PROD,
                            sameSite: "lax",
                        });
                    }
                    for (const [name, value] of Object.entries(headers)) {
                        authResponseHeaders.set(name, value);
                    }
                },
            },
        })
        : null;

    const isMutation = mutationMethods.has(context.request.method);
    if (isMutation && !bearerToken && !isSameOrigin(context.request)) {
        return new Response(JSON.stringify({
            error: {
                code: "request.origin_rejected",
                message: "The request origin could not be verified.",
                correlationId: context.locals.correlationId,
            },
        }), {
            status: 403,
            headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
        });
    }

    if (context.locals.supabase) {
        const { data } = bearerToken
            ? await context.locals.supabase.auth.getUser(bearerToken)
            : await context.locals.supabase.auth.getUser();
        context.locals.user = data.user ?? null;
        if (context.locals.user) {
            context.locals.assurance = await getAssuranceState(context.locals.supabase, bearerToken ?? undefined);
        }
        const oauthClientId = bearerToken ? jwtStringClaim(bearerToken, "client_id") : null;
        const oauthSessionId = bearerToken ? jwtStringClaim(bearerToken, "session_id") : null;
        const normalizedPath = context.url.pathname.length > 1
            ? context.url.pathname.replace(/\/+$/, "")
            : context.url.pathname;
        const isDeviceRegistration = context.request.method === "POST" &&
            normalizedPath === "/marketplace/v1/sessions";
        if (context.locals.user && oauthClientId && !isDeviceRegistration) {
            const session = oauthSessionId
                ? await context.locals.supabase.from("oauth_device_sessions")
                    .select("id,revoked_at").eq("oauth_session_id", oauthSessionId).maybeSingle()
                : { data: null, error: null };
            if (session.error || !session.data || session.data.revoked_at) {
                return new Response(JSON.stringify({
                    error: {
                        code: "account.session_revoked",
                        message: "This Hub session is not registered or has been revoked.",
                        correlationId: context.locals.correlationId,
                    },
                }), {
                    status: 401,
                    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
                });
            }
        }
    }

    const response = await next();
    response.headers.set("x-correlation-id", context.locals.correlationId);
    response.headers.set("x-content-type-options", "nosniff");
    response.headers.set("referrer-policy", context.url.pathname.startsWith("/oauth/")
        ? "no-referrer"
        : "strict-origin-when-cross-origin");
    response.headers.set("permissions-policy", "camera=(), microphone=(), geolocation=(), payment=()");
    response.headers.set("cross-origin-opener-policy", "same-origin");
    if (context.locals.user || context.url.pathname.startsWith("/account/") ||
        context.url.pathname.startsWith("/oauth/") ||
        context.url.pathname.startsWith("/marketplace/v1/library")) {
        response.headers.set("cache-control", "private, no-store");
        response.headers.set("vary", "Authorization, Cookie");
    }
    for (const [name, value] of authResponseHeaders) {
        response.headers.set(name, value);
    }
    return response;
});
