import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, i as boundedString, n as apiError, r as apiResponse, s as parseJsonObject } from "./api_CeIcXSPx.mjs";
//#region Source/pages/account/session/index.ts
var session_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
var POST = async (context) => {
	try {
		const supabase = requireSupabase(context);
		const input = await parseJsonObject(context);
		const email = boundedString(input.email, "email", 3, 254).trim().toLowerCase();
		const password = boundedString(input.password, "password", 8, 256);
		const { data, error } = await supabase.auth.signInWithPassword({
			email,
			password
		});
		if (error || !data.user) return apiResponse(context, { error: {
			code: "account.invalid_credentials",
			message: "Email or password is incorrect.",
			correlationId: context.locals.correlationId
		} }, 401);
		return apiResponse(context, { data: { userId: data.user.id } });
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/account/session/index@_@ts
var page = () => session_exports;
//#endregion
export { page };
