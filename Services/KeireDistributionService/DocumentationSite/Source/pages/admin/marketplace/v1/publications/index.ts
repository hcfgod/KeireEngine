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
        const input = await parseJsonObject(context, 96 * 1024);
        const transition = await supabase.functions.invoke("marketplace-publication", { body: input });
        if (transition.error) await throwEdgeFunctionError(transition.error);
        return apiResponse(context, {
            data: transition.data?.data ?? null,
            meta: { apiVersion: "staff/v1", correlationId: context.locals.correlationId },
        }, 201);
    } catch (error) {
        return apiError(context, error);
    }
};

