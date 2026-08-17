import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, i as boundedString, n as apiError, r as apiResponse, s as parseJsonObject, t as MarketplaceApiError } from "./api_CeIcXSPx.mjs";
//#region Source/pages/account/registration/index.ts
var registration_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
var POST = async (context) => {
	try {
		const input = await parseJsonObject(context);
		if (input.accepted !== true) throw new MarketplaceApiError(400, "account.terms_required", "Accept the platform terms and privacy notice to continue.");
		const email = boundedString(input.email, "email", 3, 254).trim().toLowerCase();
		const password = boundedString(input.password, "password", 10, 256);
		const displayName = boundedString(input.displayName, "displayName", 1, 64).trim();
		const supabase = requireSupabase(context);
		const callback = new URL("/account/callback/?next=/account/", context.url).toString();
		const { data, error } = await supabase.auth.signUp({
			email,
			password,
			options: {
				emailRedirectTo: callback,
				data: { display_name: displayName }
			}
		});
		if (error) throw new MarketplaceApiError(400, "account.registration_failed", error.message);
		if (data.session && data.user) await supabase.from("profiles").upsert({
			user_id: data.user.id,
			display_name: displayName
		}, { onConflict: "user_id" });
		return apiResponse(context, { data: { verificationRequired: !data.session } }, 202);
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/account/registration/index@_@ts
var page = () => registration_exports;
//#endregion
export { page };
