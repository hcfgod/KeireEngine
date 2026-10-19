import type { APIRoute } from "astro";
export const prerender = false;
export const POST: APIRoute = async () => new Response(JSON.stringify({ error: {
    code: "account.email_unavailable", message: "Email verification is not enabled for this provider.",
} }), { status: 503, headers: { "content-type": "application/json", "cache-control": "private, no-store" } });
