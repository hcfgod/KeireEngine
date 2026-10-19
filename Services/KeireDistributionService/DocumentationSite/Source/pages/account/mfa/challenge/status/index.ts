import type { APIRoute } from "astro";
import { apiResponse } from "../../../../../lib/api";

export const prerender = false;

// The local provider answers in middleware. Supabase uses its existing session-based challenge page.
export const GET: APIRoute = async (context) => apiResponse(context, {
    error: {
        code: "account.challenge_unavailable",
        message: "This authentication provider does not use a pending challenge lookup.",
        correlationId: context.locals.correlationId,
    },
}, 404);
