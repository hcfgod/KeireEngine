import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, parseJsonObject, requireSupabase } from "../../../lib/api";
import { getAssuranceState, requiresMfaChallenge } from "../../../lib/auth";

export const prerender = false;

export const GET: APIRoute = async (context) => apiResponse(context, {
    data: {
        signedIn: Boolean(context.locals.user),
        assuranceLevel: context.locals.user ? context.locals.assurance.currentLevel : null,
    },
});

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        const input = await parseJsonObject(context);
        const email = boundedString(input.email, "email", 3, 254).trim().toLowerCase();
        const password = boundedString(input.password, "password", 8, 256);
        const { data, error } = await supabase.auth.signInWithPassword({ email, password });
        if (error || !data.user) {
            return apiResponse(context, { error: { code: "account.invalid_credentials", message: "Email or password is incorrect.", correlationId: context.locals.correlationId } }, 401);
        }
        const assurance = await getAssuranceState(supabase);
        return apiResponse(context, { data: {
            userId: data.user.id,
            mfaRequired: requiresMfaChallenge(assurance),
        } });
    } catch (error) {
        return apiError(context, error);
    }
};
