import type { APIRoute } from "astro";

export const prerender = false;

// The local platform middleware handles recovery; other providers use their own callback flow.
export const POST: APIRoute = () => new Response(null, {
    status: 404,
    headers: { "cache-control": "private, no-store" },
});
