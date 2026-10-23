import type { APIRoute } from "astro";

export const prerender = false;

// The MySQL request adapter owns state consumption and credential exchange before Astro renders this route.
export const GET: APIRoute = () => new Response("GitHub sign-in is unavailable. Return to your account and try another sign-in method.", {
    status: 503,
    headers: { "cache-control": "private, no-store", "content-type": "text/plain; charset=utf-8",
        "referrer-policy": "no-referrer", "x-content-type-options": "nosniff" },
});
