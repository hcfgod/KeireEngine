import type { APIRoute } from "astro";
import {
    apiError, apiResponse, MarketplaceApiError, requireAal2, requireSupabase, requireUser, throwEdgeFunctionError,
} from "../../../../../lib/api";
import { featureEnabled } from "../../../../../lib/marketplace";

export const prerender = false;

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        await requireAal2(context);
        if (!await featureEnabled(supabase, "publisher_portal_enabled")) {
            throw new MarketplaceApiError(503, "publisher.disabled", "Publisher submissions are not enabled.");
        }
        const versionId = context.params.id;
        if (!versionId || !/^[0-9a-f-]{36}$/i.test(versionId)) {
            throw new MarketplaceApiError(400, "publisher.version_invalid", "The package version is invalid.");
        }
        const transition = await supabase.functions.invoke("marketplace-publisher", {
            body: { operation: "version.submit", versionId },
        });
        if (transition.error) await throwEdgeFunctionError(transition.error);
        return apiResponse(context, {
            data: transition.data?.data,
            meta: { apiVersion: "publisher/v1", correlationId: context.locals.correlationId },
        }, 201);
    } catch (error) {
        return apiError(context, error);
    }
};
