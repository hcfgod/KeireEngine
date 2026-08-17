import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, i as boundedString, l as requireUser, n as apiError, r as apiResponse, s as parseJsonObject, t as MarketplaceApiError } from "./api_CeIcXSPx.mjs";
//#region Source/pages/marketplace/v1/sessions/index.ts
var sessions_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
function jwtClaim(token, claim) {
	try {
		const parts = token.split(".");
		if (parts.length !== 3 || parts[1].length > 16384) return null;
		const value = JSON.parse(Buffer.from(parts[1], "base64url").toString("utf8"));
		return typeof value?.[claim] === "string" ? value[claim] : null;
	} catch {
		return null;
	}
}
var POST = async (context) => {
	try {
		const supabase = requireSupabase(context);
		requireUser(context);
		const token = (context.request.headers.get("authorization") ?? "").match(/^Bearer ([\x21-\x7e]{16,16384})$/i)?.[1];
		if (!token) throw new MarketplaceApiError(400, "account.oauth_token_required", "A Hub OAuth bearer token is required.");
		const sessionId = jwtClaim(token, "session_id");
		const clientId = jwtClaim(token, "client_id");
		if (!sessionId || !clientId) throw new MarketplaceApiError(400, "account.oauth_claims_required", "The token is not a Hub OAuth session.");
		const input = await parseJsonObject(context, 8192);
		const deviceName = boundedString(input.deviceName, "deviceName", 1, 128);
		const { data, error } = await supabase.rpc("register_marketplace_device_session", {
			p_oauth_session_id: sessionId,
			p_client_type: "hub",
			p_device_name: deviceName
		});
		if (error) {
			if (error.message === "device_session_revoked") throw new MarketplaceApiError(401, "account.session_revoked", "This Hub session was revoked.");
			throw error;
		}
		return apiResponse(context, {
			data: {
				id: data,
				sessionId,
				client: "hub"
			},
			meta: {
				apiVersion: "marketplace/v1",
				correlationId: context.locals.correlationId
			}
		}, 201);
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/marketplace/v1/sessions/index@_@ts
var page = () => sessions_exports;
//#endregion
export { page };
