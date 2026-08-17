import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { a as decodeCursor, c as requireSupabase, n as apiError, o as encodeCursor, r as apiResponse, t as MarketplaceApiError } from "./api_CeIcXSPx.mjs";
import { n as marketplaceEnabled } from "./marketplace_C3fGUB9S.mjs";
//#region Source/pages/marketplace/v1/catalog/index.ts
var catalog_exports = /* @__PURE__ */ __exportAll({
	GET: () => GET,
	prerender: () => false
});
var GET = async (context) => {
	try {
		const supabase = requireSupabase(context);
		if (!await marketplaceEnabled(supabase)) throw new MarketplaceApiError(503, "marketplace.disabled", "The marketplace is not enabled in this environment.");
		const offset = decodeCursor(context.url.searchParams.get("cursor"));
		const requestedLimit = Number.parseInt(context.url.searchParams.get("limit") ?? "24", 10);
		const limit = Number.isFinite(requestedLimit) ? Math.min(Math.max(requestedLimit, 1), 50) : 24;
		const search = (context.url.searchParams.get("q") ?? "").trim().slice(0, 100);
		const category = (context.url.searchParams.get("category") ?? "").trim().slice(0, 64);
		let query = supabase.from("marketplace_catalog").select("*").order("featured", { ascending: false }).order("published_at", { ascending: false }).order("id", { ascending: true }).range(offset, offset + limit);
		if (search) query = query.ilike("display_name", `%${search.replaceAll("%", "\\%").replaceAll("_", "\\_")}%`);
		if (category) query = query.eq("category_slug", category);
		const { data, error } = await query;
		if (error) throw error;
		const values = data ?? [];
		const hasMore = values.length > limit;
		return apiResponse(context, {
			data: values.slice(0, limit),
			page: {
				nextCursor: hasMore ? encodeCursor(offset + limit) : null,
				limit
			},
			meta: {
				apiVersion: "marketplace/v1",
				correlationId: context.locals.correlationId
			}
		}, 200, "public, max-age=60, stale-while-revalidate=300");
	} catch (error) {
		return apiError(context, error);
	}
};
//#endregion
//#region \0virtual:astro:page:Source/pages/marketplace/v1/catalog/index@_@ts
var page = () => catalog_exports;
//#endregion
export { page };
