import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, i as boundedString, l as requireUser, n as apiError, r as apiResponse, s as parseJsonObject, t as MarketplaceApiError } from "./api_CeIcXSPx.mjs";
import { t as featureEnabled } from "./marketplace_C3fGUB9S.mjs";
//#region Source/pages/marketplace/v1/downloads/index.ts
var downloads_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
var POST = async (context) => {
	try {
		const supabase = requireSupabase(context);
		requireUser(context);
		if (!await featureEnabled(supabase, "marketplace_enabled") || !await featureEnabled(supabase, "asset_packages_enabled")) throw new MarketplaceApiError(503, "marketplace.asset_packages_disabled", "Marketplace package downloads are not enabled.");
		const input = await parseJsonObject(context, 8192);
		const versionId = boundedString(input.versionId, "versionId", 36, 36);
		const deviceSessionId = boundedString(input.deviceSessionId, "deviceSessionId", 36, 36);
		const organizationId = input.organizationId == null ? null : boundedString(input.organizationId, "organizationId", 36, 36);
		const { data, error } = await supabase.rpc("issue_marketplace_download_grant", {
			p_version_id: versionId,
			p_device_session_id: deviceSessionId,
			p_organization_id: organizationId
		}).single();
		if (error) {
			const mapped = (/* @__PURE__ */ new Map([
				["asset_packages_disabled", [503, "marketplace.asset_packages_disabled"]],
				["device_session_invalid", [401, "account.session_revoked"]],
				["entitlement_required", [403, "marketplace.entitlement_required"]],
				["version_unavailable", [404, "marketplace.version_unavailable"]],
				["marketplace_rate_limited", [429, "marketplace.rate_limited"]]
			])).get(error.message);
			if (mapped) throw new MarketplaceApiError(mapped[0], mapped[1], error.message.replaceAll("_", " "));
			throw error;
		}
		if (!data) throw new MarketplaceApiError(500, "marketplace.download_grant_failed", "A download grant was not created.");
		const signed = await supabase.storage.from("marketplace-releases").createSignedUrl(data.storage_path, 600, { download: `keire-${versionId}.keireassetpackage` });
		if (signed.error || !signed.data?.signedUrl) throw new MarketplaceApiError(503, "marketplace.download_unavailable", "The verified package is temporarily unavailable.");
		return apiResponse(context, {
			data: {
				grantId: data.grant_id,
				url: signed.data.signedUrl,
				expiresAt: data.expires_at,
				archiveSha256: data.archive_sha256,
				archiveSizeBytes: data.archive_size_bytes
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
//#region \0virtual:astro:page:Source/pages/marketplace/v1/downloads/index@_@ts
var page = () => downloads_exports;
//#endregion
export { page };
