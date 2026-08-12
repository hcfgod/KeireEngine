import type { APIRoute } from "astro";
import {
    apiError, apiResponse, parseJsonObject, requireAal2, requireSupabase, requireUser, throwEdgeFunctionError,
} from "../../../../../lib/api";

export const prerender = false;

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        await requireAal2(context);
        const input = await parseJsonObject(context);
        const transition = await supabase.functions.invoke("marketplace-moderation", { body: input });
        if (transition.error) await throwEdgeFunctionError(transition.error);
        return apiResponse(context, {
            data: transition.data?.data ?? null,
            meta: { apiVersion: "staff/v1", correlationId: context.locals.correlationId },
        });
    } catch (error) {
        return apiError(context, error);
    }
};
