import type { APIRoute } from "astro";

export const prerender = false;

// Local resumable setup is handled by the authenticated MySQL middleware.
export const ALL: APIRoute = () => new Response(null, {
    status: 404,
    headers: { "cache-control": "private, no-store" },
});
