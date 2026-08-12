import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, parseJsonObject } from "../../lib/api";
import { runtimeEnvironment } from "../../lib/runtime-env";

export const prerender = false;
export const POST: APIRoute = async (context) => {
    try {
        const input = await parseJsonObject(context, 16 * 1024);
        if (typeof input.company === "string" && input.company) return apiResponse(context, { message: "Message received." });
        const payload = {
            company: "", name: boundedString(input.name, "name", 2, 80),
            email: boundedString(input.email, "email", 3, 254), category: boundedString(input.category, "category", 3, 32),
            subject: boundedString(input.subject, "subject", 3, 120), message: boundedString(input.message, "message", 10, 5000),
        };
        if (!new Set(["general", "support", "partnership", "press", "feedback"]).has(payload.category)) throw new MarketplaceApiError(400, "contact.invalid_category", "Choose a supported contact category.");
        const supabaseUrl = runtimeEnvironment("PUBLIC_SUPABASE_URL");
        const publishableKey = runtimeEnvironment("PUBLIC_SUPABASE_PUBLISHABLE_KEY");
        const endpoint = supabaseUrl ? `${supabaseUrl}/functions/v1/website-contact` : null;
        if (!endpoint) throw new MarketplaceApiError(503, "contact.not_configured", "Contact delivery is not configured in this environment.");
        const response = await fetch(endpoint, { method: "POST", headers: { "content-type": "application/json", ...(publishableKey ? { apikey: publishableKey } : {}) }, body: JSON.stringify(payload), signal: AbortSignal.timeout(10_000) });
        const result = await response.json().catch(() => ({}));
        if (!response.ok) throw new MarketplaceApiError(response.status, "contact.delivery_failed", typeof result.message === "string" ? result.message : "Your message could not be delivered.");
        return apiResponse(context, { message: "Your message was sent." }, 202);
    } catch (error) { return apiError(context, error); }
};
