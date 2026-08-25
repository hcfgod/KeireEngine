import type { APIRoute } from "astro";
import { getAssuranceState } from "../../../lib/auth";
import { apiError, apiResponse, boundedString, parseJsonObject, requireSupabase, requireUser } from "../../../lib/api";
import { replaceRecoveredPassword } from "../../../lib/password-recovery";

export const prerender = false;
export const PATCH: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        const assurance = await getAssuranceState(supabase);
        const input = await parseJsonObject(context);
        const password = boundedString(input.password, "password", 10, 256);
        await replaceRecoveredPassword(supabase, assurance, password);
        return apiResponse(context, { data: { updated: true, signedOut: true } });
    } catch (error) {
        return apiError(context, error);
    }
};
