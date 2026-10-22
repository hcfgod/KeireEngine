import type { APIRoute } from "astro";

// MySQL middleware owns these routes. Never fall back to a different identity provider.
export const nativeApiUnavailable: APIRoute = () => new Response(JSON.stringify({
    error: { code: "platform.native_unavailable", message: "Desktop account services are not configured on this host." },
}), {
    status: 503,
    headers: {
        "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store",
        "x-content-type-options": "nosniff",
        "referrer-policy": "no-referrer",
    },
});
