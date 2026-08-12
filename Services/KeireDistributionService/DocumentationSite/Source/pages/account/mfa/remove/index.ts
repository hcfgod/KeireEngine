import type { APIRoute } from "astro";
import { MarketplaceApiError, apiError, apiResponse, boundedString, parseJsonObject, requireSupabase, requireUser } from "../../../../lib/api";
import { getAssuranceState } from "../../../../lib/auth";

export const prerender = false;
export const POST: APIRoute = async (context) => {
    try {
        requireUser(context);
        const input = await parseJsonObject(context, 4096);
        const factorId = boundedString(input.factorId, "factorId", 36, 36);
        const supabase = requireSupabase(context);
        const factors = await supabase.auth.mfa.listFactors();
        const factor = factors.data?.all.find((candidate) => candidate.id === factorId);
        if (!factor) throw new MarketplaceApiError(404, "account.mfa_factor_missing", "The authenticator factor does not exist.");
        const assurance = await getAssuranceState(supabase);
        if (factor.status === "verified" && assurance.currentLevel !== "aal2") {
            throw new MarketplaceApiError(403, "account.mfa_required", "Verify a second factor before removing a verified authenticator.");
        }
        const { error } = await supabase.auth.mfa.unenroll({ factorId });
        if (error) throw new MarketplaceApiError(400, "account.mfa_remove_failed", "The authenticator factor could not be removed.");
        await supabase.auth.refreshSession();
        return apiResponse(context, { data: { removed: true } });
    } catch (error) {
        return apiError(context, error);
    }
};
