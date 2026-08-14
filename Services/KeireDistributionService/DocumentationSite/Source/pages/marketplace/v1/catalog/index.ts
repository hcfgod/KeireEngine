import type { APIRoute } from "astro";
import {
    apiError, apiResponse, decodeCatalogCursor, encodeCatalogCursor, MarketplaceApiError, requireSupabase,
} from "../../../../lib/api";
import { marketplaceEnabled } from "../../../../lib/marketplace";

export const prerender = false;

export const GET: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        if (!await marketplaceEnabled(supabase)) {
            throw new MarketplaceApiError(503, "marketplace.disabled", "The marketplace is not enabled in this environment.");
        }
        const cursor = decodeCatalogCursor(context.url.searchParams.get("cursor"));
        const requestedLimit = Number.parseInt(context.url.searchParams.get("limit") ?? "24", 10);
        const limit = Number.isFinite(requestedLimit) ? Math.min(Math.max(requestedLimit, 1), 50) : 24;
        const search = (context.url.searchParams.get("q") ?? "").trim().slice(0, 100);
        const category = (context.url.searchParams.get("category") ?? "").trim().slice(0, 64);
        let query = supabase.from("marketplace_catalog").select("*")
            .order("featured", { ascending: false }).order("published_at", { ascending: false })
            .order("id", { ascending: true });
        if (search) query = query.ilike("display_name", `%${search.replaceAll("%", "\\%").replaceAll("_", "\\_")}%`);
        if (category) query = query.eq("category_slug", category);
        if (cursor) {
            query = query.or(`featured.lt.${cursor.featured},and(featured.eq.${cursor.featured},published_at.lt.${cursor.publishedAt}),and(featured.eq.${cursor.featured},published_at.eq.${cursor.publishedAt},id.gt.${cursor.id})`);
        }
        query = query.limit(limit + 1);
        const { data, error } = await query;
        if (error) throw error;
        const values = data ?? [];
        const hasMore = values.length > limit;
        const pageValues = values.slice(0, limit);
        const last = pageValues.at(-1);
        return apiResponse(context, {
            data: pageValues,
            page: {
                nextCursor: hasMore && last ? encodeCatalogCursor({
                    featured: last.featured,
                    publishedAt: last.published_at,
                    id: last.id,
                }) : null,
                limit,
            },
            meta: { apiVersion: "marketplace/v1", correlationId: context.locals.correlationId },
        }, 200, "public, max-age=60, stale-while-revalidate=300");
    } catch (error) {
        return apiError(context, error);
    }
};
