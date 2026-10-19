import type { APIRoute } from "astro";

export const prerender = false;

// Register this endpoint so local recovery middleware can resolve the scoped cookie.
export const GET: APIRoute = () => new Response(null, {
    status: 404,
    headers: { "cache-control": "private, no-store" },
});
