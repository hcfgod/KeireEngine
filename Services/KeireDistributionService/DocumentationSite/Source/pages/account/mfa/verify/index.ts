import type { APIRoute } from "astro";
import { MarketplaceApiError, apiError, apiResponse, boundedString, parseJsonObject, requireSupabase, requireUser } from "../../../../lib/api";
import { getAssuranceState } from "../../../../lib/auth";

export const prerender = false;
export const POST: APIRoute = async (context) => {
    try {
        requireUser(context);
        const input = await parseJsonObject(context, 4096);
        const factorId = boundedString(input.factorId, "factorId", 36, 36);
        const code = boundedString(input.code, "code", 6, 6);
        if (!/^[0-9]{6}$/.test(code)) throw new MarketplaceApiError(400, "account.mfa_code_invalid", "Enter a six-digit authenticator code.");
        const supabase = requireSupabase(context);
        const { error } = await supabase.auth.mfa.challengeAndVerify({ factorId, code });
        if (error) throw new MarketplaceApiError(400, "account.mfa_verification_failed", "The authenticator code is invalid or expired.");
        return apiResponse(context, { data: await getAssuranceState(supabase) });
    } catch (error) {
        return apiError(context, error);
    }
};
