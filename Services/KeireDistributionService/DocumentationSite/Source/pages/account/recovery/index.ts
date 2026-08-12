import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, parseJsonObject, requireSupabase } from "../../../lib/api";
import { externalUrl } from "../../../lib/auth";

export const prerender = false;
export const POST: APIRoute = async (context) => {
    try {
        const input = await parseJsonObject(context);
        const email = boundedString(input.email, "email", 3, 254).trim().toLowerCase();
        const supabase = requireSupabase(context);
        await supabase.auth.resetPasswordForEmail(email, {
            redirectTo: externalUrl("/account/callback/?next=/account/update-password/").toString(),
        });
        return apiResponse(context, { message: "If the account exists, a recovery email has been sent." }, 202);
    } catch (error) { return apiError(context, error); }
};
