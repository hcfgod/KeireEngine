import type { APIRoute } from "astro";

export const prerender = false;

// Local platform middleware owns confirmation; Supabase retains its existing callback flow.
export const POST: APIRoute = () => new Response(null, {
    status: 404,
    headers: { "cache-control": "private, no-store" },
});
