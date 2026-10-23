import type { APIRoute } from "astro";

export const prerender = false;

// MySQL middleware authenticates and handles internal worker traffic before this route fallback.
export const ALL: APIRoute = () => new Response(null, {
    status: 404,
    headers: {
        "cache-control": "private, no-store",
        "x-content-type-options": "nosniff",
    },
});
