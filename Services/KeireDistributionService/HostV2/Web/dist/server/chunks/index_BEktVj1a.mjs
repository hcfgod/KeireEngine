import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, i as boundedString, n as apiError, r as apiResponse, s as parseJsonObject } from "./api_CeIcXSPx.mjs";
//#region Source/pages/account/recovery/index.ts
var recovery_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
var POST = async (context) => {
	try {
		const input = await parseJsonObject(context);
		const email = boundedString(input.email, "email", 3, 254).trim().toLowerCase();
		await requireSupabase(context).auth.resetPasswordForEmail(email, { redirectTo: new URL("/account/callback/?next=/account/update-password/", context.url).toString() });
		return apiResponse(context, { message: "If the account exists, a recovery email has been sent." }, 202);
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/account/recovery/index@_@ts
var page = () => recovery_exports;
//#endregion
export { page };
