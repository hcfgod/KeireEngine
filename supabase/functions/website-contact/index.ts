import { createClient } from "npm:@supabase/supabase-js@2.95.0";

const allowedOrigins = new Set([
    "https://keireengine.duckdns.org",
    "http://localhost:5098",
    "http://127.0.0.1:5098",
]);
const categories = new Set(["general", "support", "partnership", "press", "feedback"]);
const encoder = new TextEncoder();
const emailPattern = /^[^\s@]+@[^\s@]+\.[^\s@]+$/u;
const maximumRequestBytes = 16 * 1024;

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

function text(value: unknown, maximum: number): string | null {
    if (typeof value !== "string") {
        return null;
    }
    const normalized = value.trim();
    if (normalized.length > maximum || normalized.includes("\0")) {
        return null;
    }
    return normalized;
}

function requestAddress(request: Request): string {
    const forwarded = request.headers.get("x-forwarded-for")?.split(",", 1)[0]?.trim();
    return (forwarded || request.headers.get("cf-connecting-ip") || request.headers.get("x-real-ip") || "unknown")
        .slice(0, 128);
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
    const contentLength = Number(request.headers.get("content-length") ?? "0");
    if (contentType !== "application/json" || !Number.isFinite(contentLength) || contentLength > maximumRequestBytes) {
        return json(origin, 415, { ok: false, message: "A bounded JSON request is required." });
    }

    let body: Record<string, unknown>;
    try {
        body = await request.json() as Record<string, unknown>;
    } catch {
        return json(origin, 400, { ok: false, message: "The request could not be read." });
    }

    const honeypot = text(body.company, 200);
    if (honeypot === null) {
        return json(origin, 400, { ok: false, message: "Please review the form fields." });
    }
    if (honeypot.length > 0) {
        return json(origin, 202, { ok: true, message: "Thanks. Your message was received." });
    }

    const name = text(body.name, 80);
    const email = text(body.email, 254)?.toLowerCase() ?? null;
    const category = text(body.category, 32);
    const subject = text(body.subject, 120);
    const message = text(body.message, 5000);
    if (!name || name.length < 2 || !email || !emailPattern.test(email) || !category || !categories.has(category) ||
        !subject || subject.length < 3 || !message || message.length < 10) {
        return json(origin, 400, { ok: false, message: "Please review the form fields." });
    }

    const url = Deno.env.get("SUPABASE_URL") ?? "";
    const adminKey = secretKey();
    if (!url || !adminKey) {
        console.error("website-contact is missing its Supabase admin configuration");
        return json(origin, 503, { ok: false, message: "Contact is temporarily unavailable." });
    }
    const admin = createClient(url, adminKey, {
        auth: { autoRefreshToken: false, persistSession: false },
        global: { headers: { "X-Client-Info": "keire-website-contact/1.0" } },
    });
    const ipHash = await hmacSha256(requestAddress(request), adminKey);
    const { data: reserved, error: throttleError } = await admin.rpc("reserve_website_contact_submission", {
        p_ip_hash: ipHash,
    });
    if (throttleError) {
        console.error("website-contact throttle failed", throttleError.code);
        return json(origin, 503, { ok: false, message: "Contact is temporarily unavailable." });
    }
    if (reserved !== true) {
        return json(
            origin,
            429,
            { ok: false, message: "Please wait before sending another message." },
            { "Retry-After": "3600" },
        );
    }

    const { error } = await admin.from("website_contact_submissions").insert({
        name,
        email,
        category,
        subject,
        message,
    });
    if (error) {
        console.error("website-contact insert failed", error.code);
        return json(origin, 503, { ok: false, message: "Contact is temporarily unavailable." });
    }
    return json(origin, 201, { ok: true, message: "Thanks. Your message was received." });
});
