import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, n as apiError, t as MarketplaceApiError } from "./api_CeIcXSPx.mjs";
//#region Source/pages/account/callback/index.ts
var callback_exports = /* @__PURE__ */ __exportAll({
	GET: () => GET,
	prerender: () => false
});
var GET = async (context) => {
	try {
		const code = context.url.searchParams.get("code");
		if (!code) throw new MarketplaceApiError(400, "account.oauth_code_missing", "The authorization callback contains no code.");
		const { error } = await requireSupabase(context).auth.exchangeCodeForSession(code);
		if (error) throw new MarketplaceApiError(400, "account.oauth_exchange_failed", "The authorization code is invalid or expired.");
		const requested = context.url.searchParams.get("next") ?? "/account/";
		const next = requested.startsWith("/") && !requested.startsWith("//") ? requested : "/account/";
		return context.redirect(next, 303);
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/account/callback/index@_@ts
var page = () => callback_exports;
//#endregion
export { page };
