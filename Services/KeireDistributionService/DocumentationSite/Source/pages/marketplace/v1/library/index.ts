import type { APIRoute } from "astro";
import { apiError, apiResponse, decodeCursor, encodeCursor, MarketplaceApiError, requireSupabase, requireUser } from "../../../../lib/api";

export const prerender = false;
export const GET: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        const offset = decodeCursor(context.url.searchParams.get("cursor"));
        const organizationId = context.url.searchParams.get("organizationId");
        if (organizationId && !/^[0-9a-f-]{36}$/i.test(organizationId)) throw new MarketplaceApiError(400, "marketplace.invalid_organization_id", "Organization ID is invalid.");
        let query = supabase.from("marketplace_entitlements")
            .select("id,product_id,user_id,organization_id,granted_at,marketplace_products(id,slug,display_name,short_description,state,license_spdx,publishers(slug,display_name))")
            .is("revoked_at", null).order("granted_at", { ascending: false }).range(offset, offset + 24);
        query = organizationId ? query.eq("organization_id", organizationId) : query.is("organization_id", null);
        const { data, error } = await query;
        if (error) throw error;
        const values = data ?? [];
        return apiResponse(context, { data: values.slice(0, 24), page: { nextCursor: values.length > 24 ? encodeCursor(offset + 24) : null, limit: 24 }, meta: { apiVersion: "marketplace/v1", correlationId: context.locals.correlationId } });
    } catch (error) { return apiError(context, error); }
};
