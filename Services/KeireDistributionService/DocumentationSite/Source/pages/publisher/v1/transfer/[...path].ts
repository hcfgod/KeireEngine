import type { APIRoute } from "astro";

export const prerender = false;

// The MySQL middleware owns authorization and streaming. This route ensures server dispatch for PATCH/HEAD.
export const ALL: APIRoute = () => new Response(null, {
    status: 404,
    headers: { "cache-control": "private, no-store" },
});
