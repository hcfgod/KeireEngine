import type { APIRoute } from "astro";
import { apiError, requireSupabase } from "../../../lib/api";

export const prerender = false;
export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        await supabase.auth.signOut({ scope: "local" });
        return context.redirect("/", 303);
    } catch (error) {
        return apiError(context, error);
    }
};
