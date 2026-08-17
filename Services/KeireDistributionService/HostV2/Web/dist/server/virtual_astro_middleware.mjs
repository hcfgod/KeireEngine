import { A as defineMiddleware, g as sequence } from "./chunks/render_C7LtaiXC.mjs";
import { n as AstroUserError } from "./chunks/errors_BdwoJ0rW.mjs";
import { t as runtimeEnvironment } from "./chunks/runtime-env_-CSLWzci.mjs";
import { t as useTranslations } from "./chunks/translations_D8srSkSz.mjs";
import { createServerClient } from "@supabase/ssr";
import { createClient } from "@supabase/supabase-js";
//#region Source/middleware.ts
var mutationMethods = /* @__PURE__ */ new Set([
	"POST",
	"PUT",
	"PATCH",
	"DELETE"
]);
function isSameOrigin(request) {
	const origin = request.headers.get("origin");
	if (!origin) return false;
	try {
		const requestUrl = new URL(request.url);
		const forwardedProtocol = request.headers.get("x-forwarded-proto");
		const forwardedHost = request.headers.get("x-forwarded-host");
		if (forwardedProtocol && forwardedProtocol !== "http" && forwardedProtocol !== "https") return false;
		if (forwardedHost && (!/^[A-Za-z0-9.-]+(?::[0-9]{1,5})?$/.test(forwardedHost) || forwardedHost.includes(".."))) return false;
		const expectedOrigin = forwardedProtocol || forwardedHost ? `${forwardedProtocol ?? requestUrl.protocol.slice(0, -1)}://${forwardedHost ?? requestUrl.host}` : requestUrl.origin;
		return new URL(origin).origin === expectedOrigin;
	} catch {
		return false;
	}
}
function jwtStringClaim(token, claim) {
	try {
		const payload = token.split(".");
		if (payload.length !== 3 || payload[1].length > 16384) return null;
		const decoded = JSON.parse(Buffer.from(payload[1], "base64url").toString("utf8"));
		return typeof decoded?.[claim] === "string" ? decoded[claim] : null;
	} catch {
		return null;
	}
}
var onRequest$2 = defineMiddleware(async (context, next) => {
	context.locals.correlationId = crypto.randomUUID();
	context.locals.user = null;
	const supabaseUrl = runtimeEnvironment("PUBLIC_SUPABASE_URL");
	const publishableKey = runtimeEnvironment("PUBLIC_SUPABASE_PUBLISHABLE_KEY");
	const authorization = context.isPrerendered ? null : context.request.headers.get("authorization");
	const bearerMatch = authorization?.match(/^Bearer ([\x21-\x7e]{16,16384})$/i);
	if (authorization && !bearerMatch) return new Response(JSON.stringify({ error: {
		code: "account.invalid_authorization",
		message: "The authorization header is invalid.",
		correlationId: context.locals.correlationId
	} }), {
		status: 401,
		headers: {
			"content-type": "application/json; charset=utf-8",
			"cache-control": "no-store"
		}
	});
	const bearerToken = bearerMatch?.[1] ?? null;
	context.locals.supabase = supabaseUrl && publishableKey && bearerToken ? createClient(supabaseUrl, publishableKey, {
		auth: {
			persistSession: false,
			autoRefreshToken: false,
			detectSessionInUrl: false
		},
		global: { headers: { Authorization: `Bearer ${bearerToken}` } }
	}) : supabaseUrl && publishableKey ? createServerClient(supabaseUrl, publishableKey, { cookies: {
		getAll: () => context.cookies.getAll().map(({ name, value }) => ({
			name,
			value
		})),
		setAll: (cookies) => {
			for (const { name, value, options } of cookies) context.cookies.set(name, value, {
				...options,
				path: "/",
				httpOnly: true,
				secure: true,
				sameSite: "lax"
			});
		}
	} }) : null;
	if (mutationMethods.has(context.request.method) && !bearerToken && !isSameOrigin(context.request)) return new Response(JSON.stringify({ error: {
		code: "request.origin_rejected",
		message: "The request origin could not be verified.",
		correlationId: context.locals.correlationId
	} }), {
		status: 403,
		headers: {
			"content-type": "application/json; charset=utf-8",
			"cache-control": "no-store"
		}
	});
	if (context.locals.supabase) {
		const { data } = bearerToken ? await context.locals.supabase.auth.getUser(bearerToken) : await context.locals.supabase.auth.getUser();
		context.locals.user = data.user ?? null;
		const oauthClientId = bearerToken ? jwtStringClaim(bearerToken, "client_id") : null;
		const oauthSessionId = bearerToken ? jwtStringClaim(bearerToken, "session_id") : null;
		if (context.locals.user && oauthClientId) {
			const session = oauthSessionId ? await context.locals.supabase.from("oauth_device_sessions").select("id,revoked_at").eq("oauth_session_id", oauthSessionId).maybeSingle() : {
				data: null,
				error: null
			};
			if (session.error || !session.data || session.data.revoked_at) return new Response(JSON.stringify({ error: {
				code: "account.session_revoked",
				message: "This Hub session is not registered or has been revoked.",
				correlationId: context.locals.correlationId
			} }), {
				status: 401,
				headers: {
					"content-type": "application/json; charset=utf-8",
					"cache-control": "no-store"
				}
			});
		}
	}
	const response = await next();
	response.headers.set("x-correlation-id", context.locals.correlationId);
	response.headers.set("x-content-type-options", "nosniff");
	response.headers.set("referrer-policy", context.url.pathname.startsWith("/oauth/") ? "no-referrer" : "strict-origin-when-cross-origin");
	response.headers.set("permissions-policy", "camera=(), microphone=(), geolocation=(), payment=()");
	response.headers.set("cross-origin-opener-policy", "same-origin");
	if (context.locals.user || context.url.pathname.startsWith("/account/") || context.url.pathname.startsWith("/oauth/") || context.url.pathname.startsWith("/marketplace/v1/library")) {
		response.headers.set("cache-control", "private, no-store");
		response.headers.set("vary", "Authorization, Cookie");
	}
	return response;
});
//#endregion
//#region node_modules/@astrojs/starlight/locals.ts
var onRequest$1 = defineMiddleware(async (context, next) => {
	context.locals.t = useTranslations(context.currentLocale);
	initializeStarlightRoute(context);
	return next();
});
/**
* Sets up a `starlightRoute` property on locals. Initially, this will throw an error if accessed.
* When rendering, Starlight’s routes set `starlightRoute` with the resolved route data object for
* the current page.
*
* This ensures Starlight components can easily access `starlightRoute` without needing type guards,
* we can throw a helpful message if `starlightRoute` is accessed on non-Starlight pages, and we
* avoid generating route data in this middleware which also runs for non-Starlight route.
*/
function initializeStarlightRoute(context) {
	if ("starlightRoute" in context.locals) return;
	const state = { routeData: void 0 };
	Object.defineProperty(context.locals, "starlightRoute", {
		get() {
			if (!state.routeData) throw new AstroUserError("`locals.starlightRoute` is not defined", "This usually means a component that accesses `locals.starlightRoute` is being rendered outside of a Starlight page, which is not supported.\n\nIf this is a component you authored, you can do one of the following:\n\n1. Avoid using this component in non-Starlight pages.\n2. Wrap the code that reads `locals.starlightRoute` in a  `try/catch` block and handle the cases where `starlightRoute` is not available.\n\nIf this is a Starlight built-in or third-party component, you may need to report a bug or avoid this use of the component.");
			return state.routeData;
		},
		set(routeData) {
			state.routeData = routeData;
		}
	});
}
//#endregion
//#region \0virtual:astro:middleware
var onRequest = sequence(onRequest$1, onRequest$2);
//#endregion
export { onRequest };
