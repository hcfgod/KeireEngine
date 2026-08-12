import type { APIRoute } from "astro";
import { MarketplaceApiError, apiError, requireAal2, requireSupabase, requireUser } from "../../../lib/api";
import { featureEnabled } from "../../../lib/marketplace";

export const prerender = false;

export const POST: APIRoute = async (context) => {
    try {
        requireUser(context);
        const supabase = requireSupabase(context);
        if (!await featureEnabled(supabase, "hub_oauth_sso_enabled")) {
            throw new MarketplaceApiError(503, "account.hub_oauth_disabled", "Hub browser sign-in is disabled.");
        }
        if (context.locals.assurance.nextLevel === "aal2") await requireAal2(context);
        const form = await context.request.formData();
        const authorizationId = String(form.get("authorization_id") ?? "");
        const decision = String(form.get("decision") ?? "");
        if (!/^[A-Za-z0-9._~-]{16,2048}$/.test(authorizationId) || !["approve", "deny"].includes(decision)) {
            throw new MarketplaceApiError(400, "account.oauth_decision_invalid", "The authorization decision is invalid.");
        }
        const result = decision === "approve"
            ? await supabase.auth.oauth.approveAuthorization(authorizationId)
            : await supabase.auth.oauth.denyAuthorization(authorizationId);
        if (result.error || !result.data?.redirect_url) {
            throw new MarketplaceApiError(400, "account.oauth_decision_failed", "The authorization request is invalid or expired.");
        }
        return context.redirect(result.data.redirect_url, 303);
    } catch (error) {
        return apiError(context, error);
    }
};
