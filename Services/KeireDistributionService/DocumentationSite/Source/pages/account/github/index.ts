import type { APIRoute } from "astro";
import { apiError, requireSupabase } from "../../../lib/api";
import { externalUrl, safeLocalPath } from "../../../lib/auth";

export const prerender = false;
export const GET: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        const next = safeLocalPath(context.url.searchParams.get("next"));
        const callback = externalUrl("/account/callback/");
        callback.searchParams.set("next", next);
        const credentials = {
            provider: "github" as const,
            options: { redirectTo: callback.toString(), skipBrowserRedirect: true },
        };
        const { data, error } = context.locals.user
            ? await supabase.auth.linkIdentity(credentials)
            : await supabase.auth.signInWithOAuth(credentials);
        if (error || !data.url) throw error ?? new Error("GitHub authorization URL was not returned.");
        return context.redirect(data.url, 303);
    } catch (error) {
        return apiError(context, error);
    }
};
