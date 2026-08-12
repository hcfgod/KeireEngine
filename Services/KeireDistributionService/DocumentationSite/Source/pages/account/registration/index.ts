import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, parseJsonObject, requireSupabase } from "../../../lib/api";
import { externalUrl } from "../../../lib/auth";

export const prerender = false;
export const POST: APIRoute = async (context) => {
    try {
        const input = await parseJsonObject(context);
        if (input.accepted !== true) throw new MarketplaceApiError(400, "account.terms_required", "Accept the platform terms and privacy notice to continue.");
        const email = boundedString(input.email, "email", 3, 254).trim().toLowerCase();
        const password = boundedString(input.password, "password", 10, 256);
        const displayName = boundedString(input.displayName, "displayName", 1, 64).trim();
        const supabase = requireSupabase(context);
        const callback = externalUrl("/account/callback/?next=/account/").toString();
        const { data, error } = await supabase.auth.signUp({ email, password, options: { emailRedirectTo: callback, data: { display_name: displayName } } });
        if (error) throw new MarketplaceApiError(400, "account.registration_failed", error.message);
        if (data.session && data.user) await supabase.from("profiles").upsert({ user_id: data.user.id, display_name: displayName }, { onConflict: "user_id" });
        return apiResponse(context, { data: { verificationRequired: !data.session } }, 202);
    } catch (error) { return apiError(context, error); }
};
