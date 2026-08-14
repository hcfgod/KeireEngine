import type { APIRoute } from "astro";
import {
    apiError, apiResponse, decodeLibraryCursor, encodeLibraryCursor, MarketplaceApiError, requireSupabase, requireUser,
} from "../../../../lib/api";

export const prerender = false;
export const GET: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        const cursor = decodeLibraryCursor(context.url.searchParams.get("cursor"));
        const organizationId = context.url.searchParams.get("organizationId");
        if (organizationId && !/^[0-9a-f-]{36}$/i.test(organizationId)) throw new MarketplaceApiError(400, "marketplace.invalid_organization_id", "Organization ID is invalid.");
        let query = supabase.from("marketplace_entitlements")
            .select("id,product_id,user_id,organization_id,granted_at,marketplace_products(id,slug,display_name,short_description,state,license_spdx,publishers(slug,display_name))")
            .is("revoked_at", null).order("granted_at", { ascending: false }).order("id", { ascending: true });
        query = organizationId ? query.eq("organization_id", organizationId) : query.is("organization_id", null);
        if (cursor) {
            query = query.or(`granted_at.lt.${cursor.grantedAt},and(granted_at.eq.${cursor.grantedAt},id.gt.${cursor.id})`);
        }
        query = query.limit(25);
        const { data, error } = await query;
        if (error) throw error;
        const values = data ?? [];
        const pageValues = values.slice(0, 24);
        const last = pageValues.at(-1);
        return apiResponse(context, {
            data: pageValues,
            page: {
                nextCursor: values.length > 24 && last ? encodeLibraryCursor({
                    grantedAt: last.granted_at,
                    id: last.id,
                }) : null,
                limit: 24,
            },
            meta: { apiVersion: "marketplace/v1", correlationId: context.locals.correlationId },
        });
    } catch (error) { return apiError(context, error); }
};
