import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { c as requireSupabase, i as boundedString, l as requireUser, n as apiError, r as apiResponse, s as parseJsonObject, t as MarketplaceApiError } from "./api_CeIcXSPx.mjs";
//#region Source/pages/marketplace/v1/claims/index.ts
var claims_exports = /* @__PURE__ */ __exportAll({
	POST: () => POST,
	prerender: () => false
});
var POST = async (context) => {
	try {
		const supabase = requireSupabase(context);
		requireUser(context);
		const input = await parseJsonObject(context);
		const productId = boundedString(input.productId, "productId", 36, 36);
		const ownership = input.ownership === "organization" ? "organization" : "personal";
		const organizationId = ownership === "organization" ? boundedString(input.organizationId, "organizationId", 36, 36) : null;
		const idempotencyKey = context.request.headers.get("idempotency-key");
		if (!idempotencyKey || idempotencyKey.length < 16 || idempotencyKey.length > 128) throw new MarketplaceApiError(400, "request.idempotency_key_required", "A bounded Idempotency-Key header is required.");
		const { data: product, error: productError } = await supabase.from("marketplace_catalog").select("id,license_spdx,license_revision,license_acceptance_snapshot").eq("id", productId).maybeSingle();
		if (productError) throw productError;
		if (!product) throw new MarketplaceApiError(404, "marketplace.product_not_found", "Product was not found.");
		const licenseSnapshot = boundedString(product.license_acceptance_snapshot, "licenseAcceptanceSnapshot", 1, 1e5);
		const { data, error } = await supabase.rpc("claim_free_marketplace_product", {
			p_product_id: productId,
			p_organization_id: organizationId,
			p_idempotency_key: idempotencyKey,
			p_license_spdx: product.license_spdx,
			p_license_revision: product.license_revision,
			p_accepted_license_snapshot: licenseSnapshot
		});
		if (error) {
			const mapped = (/* @__PURE__ */ new Map([
				["marketplace_disabled", [503, "marketplace.disabled"]],
				["organization_authorization_required", [403, "marketplace.organization_forbidden"]],
				["license_revision_changed", [409, "marketplace.license_changed"]],
				["product_not_found", [404, "marketplace.product_not_found"]]
			])).get(error.message);
			if (mapped) throw new MarketplaceApiError(mapped[0], mapped[1], error.message.replaceAll("_", " "));
			throw error;
		}
		return apiResponse(context, {
			data: {
				entitlementId: data,
				ownership,
				organizationId
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
//#region \0virtual:astro:page:Source/pages/marketplace/v1/claims/index@_@ts
var page = () => claims_exports;
//#endregion
export { page };
