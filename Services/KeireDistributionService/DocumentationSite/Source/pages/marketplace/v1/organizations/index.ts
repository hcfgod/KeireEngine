import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, parseJsonObject, requireAal2,
    requireSupabase, throwEdgeFunctionError } from "../../../../lib/api";

export const prerender = false;

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        await requireAal2(context);
        const input = await parseJsonObject(context, 8 * 1024);
        const slug = boundedString(input.slug, "slug", 3, 63).trim().toLowerCase();
        if (!/^[a-z0-9][a-z0-9-]{2,62}$/.test(slug)) {
            throw new MarketplaceApiError(400, "organization.slug_invalid",
                "Use lowercase letters, numbers, and hyphens for the organization URL.");
        }
        const displayName = boundedString(input.displayName, "displayName", 1, 96).trim();
        const { data, error } = await supabase.functions.invoke("marketplace-library", {
            body: { operation: "organization.create", slug, displayName },
        });
        if (error) await throwEdgeFunctionError(error);
        return apiResponse(context, {
            data: { organizationId: data?.data?.organizationId, slug, displayName },
            meta: { apiVersion: "marketplace/v1", correlationId: context.locals.correlationId },
        }, 201);
    } catch (error) {
        return apiError(context, error);
    }
};
