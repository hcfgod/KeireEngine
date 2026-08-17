import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, l as requireUser, n as apiError, t as MarketplaceApiError } from "./api_CeIcXSPx.mjs";
import { t as featureEnabled } from "./marketplace_C3fGUB9S.mjs";
//#region Source/pages/oauth/decision/index.ts
var decision_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
var POST = async (context) => {
	try {
		requireUser(context);
		const supabase = requireSupabase(context);
		if (!await featureEnabled(supabase, "hub_oauth_sso_enabled")) throw new MarketplaceApiError(503, "account.hub_oauth_disabled", "Hub browser sign-in is disabled.");
		const form = await context.request.formData();
		const authorizationId = String(form.get("authorization_id") ?? "");
		const decision = String(form.get("decision") ?? "");
		if (!/^[A-Za-z0-9._~-]{16,2048}$/.test(authorizationId) || !["approve", "deny"].includes(decision)) throw new MarketplaceApiError(400, "account.oauth_decision_invalid", "The authorization decision is invalid.");
		const result = decision === "approve" ? await supabase.auth.oauth.approveAuthorization(authorizationId) : await supabase.auth.oauth.denyAuthorization(authorizationId);
		if (result.error || !result.data?.redirect_url) throw new MarketplaceApiError(400, "account.oauth_decision_failed", "The authorization request is invalid or expired.");
		return context.redirect(result.data.redirect_url, 303);
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/oauth/decision/index@_@ts
var page = () => decision_exports;
//#endregion
export { page };
