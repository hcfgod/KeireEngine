import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, parseJsonObject, requireSupabase, requireUser,
    throwEdgeFunctionError } from "../../../../lib/api";

export const prerender = false;
export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        const input = await parseJsonObject(context);
        const productId = boundedString(input.productId, "productId", 36, 36);
        const ownership = input.ownership === "organization" ? "organization" : "personal";
        const organizationId = ownership === "organization" ? boundedString(input.organizationId, "organizationId", 36, 36) : null;
        const idempotencyKey = context.request.headers.get("idempotency-key");
        if (!idempotencyKey || idempotencyKey.length < 16 || idempotencyKey.length > 128) throw new MarketplaceApiError(400, "request.idempotency_key_required", "A bounded Idempotency-Key header is required.");
        const { data: product, error: productError } = await supabase.from("marketplace_catalog")
            .select("id,license_spdx,license_revision,license_acceptance_snapshot").eq("id", productId).maybeSingle();
        if (productError) throw productError;
        if (!product) throw new MarketplaceApiError(404, "marketplace.product_not_found", "Product was not found.");
        const licenseSnapshot = boundedString(product.license_acceptance_snapshot, "licenseAcceptanceSnapshot", 1, 100_000);
        const { data, error } = await supabase.functions.invoke("marketplace-library", { body: {
            operation: "claim.create", productId, organizationId, idempotencyKey,
            licenseSpdx: product.license_spdx, licenseRevision: product.license_revision,
            acceptedLicenseSnapshot: licenseSnapshot,
        } });
        if (error) {
            await throwEdgeFunctionError(error);
        }
        return apiResponse(context, { data: { entitlementId: data?.data?.entitlementId, ownership, organizationId }, meta: { apiVersion: "marketplace/v1", correlationId: context.locals.correlationId } }, 201);
    } catch (error) { return apiError(context, error); }
};
