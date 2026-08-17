import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, i as boundedString, l as requireUser, n as apiError, r as apiResponse, s as parseJsonObject } from "./api_CeIcXSPx.mjs";
//#region Source/pages/account/password/index.ts
var password_exports = /* @__PURE__ */ __exportAll({
	PATCH: () => PATCH,
	prerender: () => false
});
var PATCH = async (context) => {
	try {
		const supabase = requireSupabase(context);
		requireUser(context);
		const input = await parseJsonObject(context);
		const password = boundedString(input.password, "password", 10, 256);
		const { error } = await supabase.auth.updateUser({ password });
		if (error) throw error;
		return apiResponse(context, { data: { updated: true } });
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/account/password/index@_@ts
var page = () => password_exports;
//#endregion
export { page };
