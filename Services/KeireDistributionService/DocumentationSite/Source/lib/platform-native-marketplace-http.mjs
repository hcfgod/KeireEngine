import { readObject } from "./platform-auth-http.mjs";
import { LocalClaimError } from "./platform-claims.mjs";
import { registerNativeDevice } from "./platform-native-devices.mjs";

export function createNativeMarketplaceHttp({ pool, siteOrigin, clientId }) {
    return async (request) => {
        const url = new URL(request.url);
        if (url.pathname.replace(/\/+$/, "") !== "/functions/v1/marketplace-hub") {
            return null;
        }
        const headers = { "cache-control": "private, no-store", "x-content-type-options": "nosniff" };
        const fail = (status, code, message) => Response.json({ error: { code, message } }, { status, headers });
        if (request.method !== "POST") {
            return new Response(null, { status: 405, headers: { ...headers, allow: "POST" } });
        }
        if (url.origin !== siteOrigin || url.search || request.headers.has("cookie") || request.headers.has("origin")
            || request.headers.has("apikey")) {
            return fail(400, "request.invalid", "Use the registered desktop marketplace service.");
        }
        const token = /^Bearer ([A-Za-z0-9_-]{43})$/.exec(request.headers.get("authorization") ?? "")?.[1];
        if (!token) {
            return fail(401, "account.sign_in_required", "Sign in again to continue.");
        }
        let input;
        try {
            input = await readObject(request, 4096);
        } catch {
            return fail(400, "request.invalid", "The marketplace request is invalid or too large.");
        }
        if (input.operation !== "session.register" || Object.keys(input).sort().join(",") !== "deviceName,operation") {
            return fail(400, "request.operation_invalid", "This marketplace operation is not supported.");
        }
        try {
            const data = await registerNativeDevice(pool, token, clientId, input.deviceName);
            return Response.json({ data }, { status: 201, headers });
        } catch (error) {
            if (error instanceof LocalClaimError) {
                return fail(error.status, error.code, error.message);
            }
            return fail(503, "marketplace.unavailable", "The marketplace is temporarily unavailable. Please try again shortly.");
        }
    };
}
