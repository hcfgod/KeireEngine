import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, n as apiError } from "./api_CeIcXSPx.mjs";
//#region Source/pages/account/session/delete.ts
var delete_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
var POST = async (context) => {
	try {
		await requireSupabase(context).auth.signOut({ scope: "local" });
		return context.redirect("/", 303);
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/account/session/delete@_@ts
var page = () => delete_exports;
//#endregion
export { page };
