import { withNativeSessionTransaction } from "./platform-native-sessions.mjs";

export function createNativeAccountHttp({ pool, siteOrigin, clientId }) {
    return async (request) => {
        const url = new URL(request.url);
        const pathname = url.pathname.replace(/\/+$/, "");
        if (!["/auth/v1/user", "/auth/v1/logout"].includes(pathname)) {
            return null;
        }
        const headers = { "cache-control": "private, no-store", "x-content-type-options": "nosniff" };
        const fail = (status, message) => Response.json({ message }, { status, headers });
        const logout = pathname === "/auth/v1/logout";
        const method = logout ? "POST" : "GET";
        if (request.method !== method) {
            return new Response(null, { status: 405, headers: { ...headers, allow: method } });
        }
        if (url.origin !== siteOrigin || request.headers.has("cookie") || request.headers.has("origin")
            || request.headers.has("apikey") || (!logout && url.search)
            || (logout && (url.searchParams.size !== 1 || url.searchParams.get("scope") !== "local"))) {
            return fail(400, "Use the registered desktop account service.");
        }
        const authorization = request.headers.get("authorization") ?? "";
        const token = /^Bearer ([A-Za-z0-9_-]{43})$/.exec(authorization)?.[1];
        if (!token) {
            return fail(401, "Sign in again to continue.");
        }
        try {
            return await withNativeSessionTransaction(pool, token, async (connection, identity) => {
                if (identity.clientId !== clientId) {
                    return fail(401, "Sign in again to continue.");
                }
                if (logout) {
                    await connection.execute("UPDATE platform_native_sessions SET revoked_ms=? WHERE id=?",
                        [Date.now(), identity.sessionId]);
                    return new Response(null, { status: 204, headers });
                }
                if (!identity.scope.split(" ").includes("email")) {
                    return fail(403, "This desktop session does not have email access. Sign in again from Kéire Hub.");
                }
                return Response.json({ id: identity.userId, email: identity.account.email,
                    email_confirmed_at: identity.account.email_confirmed_at }, { headers });
            }) ?? fail(401, "This desktop session expired or was revoked. Sign in again to continue.");
        } catch {
            return fail(503, "The account service is temporarily unavailable. Please try again shortly.");
        }
    };
}
