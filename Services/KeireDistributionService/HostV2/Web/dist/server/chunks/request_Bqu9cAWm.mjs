import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, l as requireUser, n as apiError, r as apiResponse, s as parseJsonObject, t as MarketplaceApiError } from "./api_CeIcXSPx.mjs";
//#region Source/pages/account/data/request.ts
var request_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
var POST = async (context) => {
	try {
		const supabase = requireSupabase(context);
		const user = requireUser(context);
		const input = await parseJsonObject(context);
		if (input.kind !== "export" && input.kind !== "deletion") throw new MarketplaceApiError(400, "account.invalid_data_request", "Data request kind is invalid.");
		const field = input.kind === "export" ? "export_requested_at" : "deletion_requested_at";
		const { error } = await supabase.from("profiles").update({ [field]: (/* @__PURE__ */ new Date()).toISOString() }).eq("user_id", user.id);
		if (error) throw error;
		return apiResponse(context, { data: {
			kind: input.kind,
			requested: true
		} }, 202);
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/account/data/request@_@ts
var page = () => request_exports;
//#endregion
export { page };
