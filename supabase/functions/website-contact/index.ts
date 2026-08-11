import { createClient } from "npm:@supabase/supabase-js@2.95.0";

import { ContactRequestError, readBoundedJson, requestAddress } from "./request.mjs";
import { normalizeText, unicodeLength } from "./validation.mjs";

const allowedOrigins = new Set([
    "https://keireengine.duckdns.org",
    "http://localhost:5098",
    "http://127.0.0.1:5098",
]);
const categories = new Set(["general", "support", "partnership", "press", "feedback"]);
const encoder = new TextEncoder();
const emailPattern = /^[^\s@]+@[^\s@]+\.[^\s@]+$/u;

function responseHeaders(origin: string): HeadersInit {
    return {
        "Access-Control-Allow-Headers": "content-type",
        "Access-Control-Allow-Methods": "POST, OPTIONS",
        "Access-Control-Allow-Origin": origin,
        "Access-Control-Max-Age": "86400",
        "Cache-Control": "no-store",
        "Content-Type": "application/json; charset=utf-8",
        "Vary": "Origin",
    };
}

function json(origin: string, status: number, body: Record<string, unknown>, extra: HeadersInit = {}): Response {
    return new Response(JSON.stringify(body), {
        status,
        headers: { ...responseHeaders(origin), ...extra },
    });
}

async function hmacSha256(value: string, secret: string): Promise<string> {
    const key = await crypto.subtle.importKey(
        "raw",
        encoder.encode(secret),
        { name: "HMAC", hash: "SHA-256" },
        false,
        ["sign"],
    );
    const digest = new Uint8Array(await crypto.subtle.sign("HMAC", key, encoder.encode(value)));
    return Array.from(digest, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function secretKey(): string | null {
    try {
        const keys = JSON.parse(Deno.env.get("SUPABASE_SECRET_KEYS") ?? "{}") as Record<string, unknown>;
        if (typeof keys.default === "string" && keys.default.length > 0) {
            return keys.default;
        }
    } catch {
        // The legacy variable below keeps local and transitional deployments usable.
    }
    return Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? null;
}

Deno.serve(async (request: Request) => {
    const origin = request.headers.get("origin") ?? "";
    if (!allowedOrigins.has(origin)) {
        return new Response(JSON.stringify({ ok: false, message: "Origin not allowed." }), {
            status: 403,
            headers: { "Cache-Control": "no-store", "Content-Type": "application/json; charset=utf-8" },
        });
    }
    if (request.method === "OPTIONS") {
        return new Response(null, { status: 204, headers: responseHeaders(origin) });
    }
    if (request.method !== "POST") {
        return json(origin, 405, { ok: false, message: "Method not allowed." }, { Allow: "POST, OPTIONS" });
    }
    const contentType = request.headers.get("content-type")?.split(";", 1)[0]?.trim().toLowerCase();
    if (contentType !== "application/json") {
        return json(origin, 415, { ok: false, message: "A JSON request is required." });
    }

    let body: Record<string, unknown>;
    try {
        body = await readBoundedJson(request);
    } catch (error) {
        if (error instanceof ContactRequestError) {
            return json(origin, error.status, { ok: false, message: error.message });
        }
        return json(origin, 400, { ok: false, message: "The request could not be read." });
    }

    const honeypot = normalizeText(body.company, 200);
    if (honeypot === null) {
        return json(origin, 400, { ok: false, message: "Please review the form fields." });
    }
    if (honeypot.length > 0) {
        return json(origin, 202, { ok: true, message: "Thanks. Your message was received." });
    }

    const name = normalizeText(body.name, 80);
    const email = normalizeText(body.email, 254)?.toLowerCase() ?? null;
    const category = normalizeText(body.category, 32);
    const subject = normalizeText(body.subject, 120);
    const message = normalizeText(body.message, 5000);
    if (!name || unicodeLength(name) < 2 || !email || !emailPattern.test(email) || !category ||
        !categories.has(category) || !subject || unicodeLength(subject) < 3 || !message ||
        unicodeLength(message) < 10) {
        return json(origin, 400, { ok: false, message: "Please review the form fields." });
    }

    const url = Deno.env.get("SUPABASE_URL") ?? "";
    const adminKey = secretKey();
    const rateLimitSecret = Deno.env.get("CONTACT_RATE_LIMIT_SECRET") ?? "";
    if (!url || !adminKey || !rateLimitSecret) {
        console.error("website-contact is missing its server-side configuration");
        return json(origin, 503, { ok: false, message: "Contact is temporarily unavailable." });
    }
    const admin = createClient(url, adminKey, {
        auth: { autoRefreshToken: false, persistSession: false },
        global: { headers: { "X-Client-Info": "keire-website-contact/1.0" } },
    });
    const ipHash = await hmacSha256(requestAddress(request), rateLimitSecret);
    const { data: accepted, error: submissionError } = await admin.rpc("submit_website_contact", {
        p_ip_hash: ipHash,
        p_name: name,
        p_email: email,
        p_category: category,
        p_subject: subject,
        p_message: message,
    });
    if (submissionError) {
        console.error("website-contact transaction failed", submissionError.code);
        return json(origin, 503, { ok: false, message: "Contact is temporarily unavailable." });
    }
    if (accepted !== true) {
        return json(
            origin,
            429,
            { ok: false, message: "Please wait before sending another message." },
            { "Retry-After": "3600" },
        );
    }
    return json(origin, 201, { ok: true, message: "Thanks. Your message was received." });
});
