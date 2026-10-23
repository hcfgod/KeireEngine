import type { APIRoute } from "astro";

export const prerender = false;

// MySQL middleware authorizes and serves private bytes before this fallback. Supabase uses its existing signed URLs.
export const ALL: APIRoute = async () => new Response(null, {
    status: 404,
    headers: { "cache-control": "private, no-store", "x-content-type-options": "nosniff" },
});
