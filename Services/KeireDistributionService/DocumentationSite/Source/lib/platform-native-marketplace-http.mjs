import { readObject } from "./platform-auth-http.mjs";
import { LocalClaimError } from "./platform-claims.mjs";
import { registerNativeDevice } from "./platform-native-devices.mjs";
import { issueNativeDownloadGrant } from "./platform-native-download-grants.mjs";

export function createNativeMarketplaceHttp({ pool, siteOrigin, clientId, capabilities, packageStore }) {
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
        const keys = Object.keys(input).sort().join(",");
        const registration = input.operation === "session.register" && keys === "deviceName,operation";
        const download = input.operation === "download.issue"
            && ["deviceSessionId,operation,versionId", "deviceSessionId,operation,organizationId,versionId"].includes(keys);
        if (!registration && !download) {
            return fail(400, "request.operation_invalid", "This marketplace operation is not supported.");
        }
        try {
            if (download) {
                if (!capabilities || !packageStore) {
                    return fail(503, "marketplace.unavailable", "Asset downloads are temporarily unavailable.");
                }
                const grant = await issueNativeDownloadGrant(pool, packageStore, token, clientId, {
                    versionId: input.versionId, deviceSessionId: input.deviceSessionId,
                    organizationId: input.organizationId ?? null,
                });
                const url = capabilities.issue(grant.grant_id, grant.expires_at);
                return Response.json({ data: { ...grant, url } }, { status: 201, headers });
            }
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
