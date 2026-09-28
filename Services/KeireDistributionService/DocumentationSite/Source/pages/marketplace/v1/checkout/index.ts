import type { APIRoute } from "astro";
import { apiError, boundedString, MarketplaceApiError, parseJsonObject, requireSupabase, requireUser } from "../../../../lib/api";
import { loadOffer, purchaseState } from "../../../../lib/marketplace-purchase";

export const prerender = false;

// Reserve a stable checkout boundary without creating orders, entitlements, or payment sessions.
export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        const input = await parseJsonObject(context);
        const productId = boundedString(input.productId, "productId", 36, 36);
        const { data: product, error } = await supabase.from("marketplace_catalog")
            .select("id").eq("id", productId).maybeSingle();
        if (error) throw error;
        if (!product) throw new MarketplaceApiError(404, "marketplace.product_not_found", "Product was not found.");
        const acquisition = purchaseState(await loadOffer(supabase, productId));
        if (acquisition.canClaim) throw new MarketplaceApiError(409, "marketplace.free_claim_required",
            "This asset is free. Use Get asset on its listing to add it to My Assets. No payment is needed.");
        throw new MarketplaceApiError(409,
            acquisition.state === "purchase_unavailable" ? "marketplace.purchase_unavailable" : "marketplace.offer_unavailable",
            acquisition.message);
    } catch (error) { return apiError(context, error); }
};
