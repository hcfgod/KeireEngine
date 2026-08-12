import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, parseJsonObject, requireSupabase, requireUser,
    throwEdgeFunctionError } from "../../../../lib/api";

export const prerender = false;

function jwtClaim(token: string, claim: string): string | null {
    try {
        const parts = token.split(".");
        if (parts.length !== 3 || parts[1].length > 16 * 1024) return null;
        const value = JSON.parse(Buffer.from(parts[1], "base64url").toString("utf8"));
        return typeof value?.[claim] === "string" ? value[claim] : null;
    } catch { return null; }
}

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        const authorization = context.request.headers.get("authorization") ?? "";
        const token = authorization.match(/^Bearer ([\x21-\x7e]{16,16384})$/i)?.[1];
        if (!token) throw new MarketplaceApiError(400, "account.oauth_token_required", "A Hub OAuth bearer token is required.");
        const sessionId = jwtClaim(token, "session_id");
        const clientId = jwtClaim(token, "client_id");
        if (!sessionId || !clientId) throw new MarketplaceApiError(400, "account.oauth_claims_required", "The token is not a Hub OAuth session.");
        const input = await parseJsonObject(context, 8 * 1024);
        const deviceName = boundedString(input.deviceName, "deviceName", 1, 128);
        const { data, error } = await supabase.functions.invoke("marketplace-hub", {
            body: { operation: "session.register", deviceName },
        });
        if (error) {
            await throwEdgeFunctionError(error);
        }
        return apiResponse(context, { data: { id: data?.data?.deviceSessionId, sessionId, client: "hub" }, meta: { apiVersion: "marketplace/v1", correlationId: context.locals.correlationId } }, 201);
    } catch (error) { return apiError(context, error); }
};
