import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, n as apiError } from "./api_CeIcXSPx.mjs";
//#region Source/pages/account/github/index.ts
var github_exports = /* @__PURE__ */ __exportAll({
	GET: () => GET,
	prerender: () => false
});
var GET = async (context) => {
	try {
		const supabase = requireSupabase(context);
		const requested = context.url.searchParams.get("next") ?? "/account/";
		const next = requested.startsWith("/") && !requested.startsWith("//") ? requested : "/account/";
		const callback = new URL("/account/callback/", context.url);
		callback.searchParams.set("next", next);
		const { data, error } = await supabase.auth.signInWithOAuth({
			provider: "github",
			options: {
				redirectTo: callback.toString(),
				skipBrowserRedirect: true
			}
		});
		if (error || !data.url) throw error ?? /* @__PURE__ */ new Error("GitHub authorization URL was not returned.");
		return context.redirect(data.url, 303);
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/account/github/index@_@ts
var page = () => github_exports;
//#endregion
export { page };
