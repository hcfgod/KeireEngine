import type { APIRoute } from "astro";
import { apiError, apiResponse, MarketplaceApiError, requireSupabase } from "../../../../lib/api";
import { marketplaceEnabled } from "../../../../lib/marketplace";

export const prerender = false;
export const GET: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        if (!await marketplaceEnabled(supabase)) throw new MarketplaceApiError(503, "marketplace.disabled", "The marketplace is disabled.");
        const id = context.params.id;
        if (!id || !/^[0-9a-f-]{36}$/i.test(id)) throw new MarketplaceApiError(400, "marketplace.invalid_product_id", "Product ID is invalid.");
        const [productResult, versionsResult, mediaResult, reviewsResult] = await Promise.all([
            supabase.from("marketplace_catalog").select("*").eq("id", id).maybeSingle(),
            supabase.from("marketplace_product_versions").select("id,version,state,install_kind,minimum_engine_version,maximum_engine_version,platforms,architectures,renderer_capabilities,managed_api_version,release_notes_markdown,published_at").eq("product_id", id).in("state", ["published", "withdrawn", "security_revoked"]).order("published_at", { ascending: false }),
            supabase.from("marketplace_product_media").select("id,storage_path,media_type,alt_text,width,height,sort_order,sha256").eq("product_id", id).not("approved_at", "is", null).order("sort_order"),
            supabase.from("marketplace_reviews").select("id,rating,title,body,publisher_reply,created_at,updated_at").eq("product_id", id).is("hidden_at", null).order("created_at", { ascending: false }).limit(50),
        ]);
        if (productResult.error) throw productResult.error;
        if (!productResult.data) throw new MarketplaceApiError(404, "marketplace.product_not_found", "Product was not found.");
        for (const result of [versionsResult, mediaResult, reviewsResult]) if (result.error) throw result.error;
        return apiResponse(context, { data: { ...productResult.data, versions: versionsResult.data, media: mediaResult.data, reviews: reviewsResult.data }, meta: { apiVersion: "marketplace/v1", correlationId: context.locals.correlationId } }, 200, "public, max-age=60, stale-while-revalidate=300");
    } catch (error) { return apiError(context, error); }
};
