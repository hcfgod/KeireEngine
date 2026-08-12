import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, requireAal2,
    requireSupabase, requireUser, throwEdgeFunctionError } from "../../../../../lib/api";
import { featureEnabled } from "../../../../../lib/marketplace";

export const prerender = false;

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        await requireAal2(context);
        if (!await featureEnabled(supabase, "publisher_portal_enabled")) {
            throw new MarketplaceApiError(503, "publisher.disabled", "Publisher uploads are not enabled.");
        }
        const uploadId = boundedString(context.params.id, "uploadId", 36, 36);
        const transition = await supabase.functions.invoke("marketplace-publisher", {
            body: { operation: "upload.complete", uploadId },
        });
        if (transition.error) await throwEdgeFunctionError(transition.error);
        const data = transition.data?.data;
        if (!data || typeof data.uploadId !== "string" || typeof data.versionId !== "string" ||
            data.state !== "uploaded") {
            throw new MarketplaceApiError(503, "publisher.upload_completion_failed",
                "The upload completion response was incomplete.");
        }
        return apiResponse(context, {
            data,
            meta: { apiVersion: "publisher/v1", correlationId: context.locals.correlationId },
        });
    } catch (error) {
        return apiError(context, error);
    }
};
