import type { APIRoute } from "astro";
import { apiError, apiResponse, MarketplaceApiError, parseJsonObject, requireAal2,
    requireSupabase, requireUser, throwEdgeFunctionError } from "../../../../lib/api";
import { featureEnabled } from "../../../../lib/marketplace";

export const prerender = false;

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        await requireAal2(context);
        if (!await featureEnabled(supabase, "publisher_portal_enabled")) {
            throw new MarketplaceApiError(503, "publisher.disabled", "Publisher uploads are not enabled.");
        }
        const input = await parseJsonObject(context);
        const transition = await supabase.functions.invoke("marketplace-publisher", {
            body: { ...input, operation: "upload.reserve" },
        });
        if (transition.error) await throwEdgeFunctionError(transition.error);
        const data = transition.data?.data;
        if (!data || typeof data.uploadId !== "string" || typeof data.versionId !== "string" ||
            typeof data.productId !== "string" ||
            typeof data.bucket !== "string" || typeof data.storagePath !== "string" ||
            typeof data.uploadToken !== "string" || typeof data.resumableEndpoint !== "string" ||
            typeof data.expiresAt !== "string") {
            throw new MarketplaceApiError(503, "publisher.upload_reservation_failed",
                "The upload reservation response was incomplete.");
        }
        return apiResponse(context, {
            data,
            meta: { apiVersion: "publisher/v1", correlationId: context.locals.correlationId },
        }, 201);
    } catch (error) {
        return apiError(context, error);
    }
};
