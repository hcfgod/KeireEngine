import { withNativeSessionTransaction } from "./platform-native-sessions.mjs";
import { readObject } from "./platform-auth-http.mjs";

export function createNativeAccountHttp({ pool, siteOrigin, clientId }) {
    return async (request) => {
        const url = new URL(request.url);
        const pathname = url.pathname.replace(/\/+$/, "");
        if (!["/auth/v1/user", "/auth/v1/logout", "/rest/v1/profiles"].includes(pathname)) {
            return null;
        }
        const headers = { "cache-control": "private, no-store", "x-content-type-options": "nosniff" };
        const fail = (status, message) => Response.json({ message }, { status, headers });
        const logout = pathname === "/auth/v1/logout";
        const profile = pathname === "/rest/v1/profiles";
        const saveProfile = profile && request.method === "POST";
        const method = logout ? "POST" : "GET";
        if (request.method !== method && !saveProfile) {
            return new Response(null, { status: 405, headers: { ...headers, allow: profile ? "GET, POST" : method } });
        }
        if (url.origin !== siteOrigin || request.headers.has("cookie") || request.headers.has("origin")
            || request.headers.has("apikey") || (!logout && !profile && url.search)
            || (logout && (url.searchParams.size !== 1 || url.searchParams.get("scope") !== "local"))) {
            return fail(400, "Use the registered desktop account service.");
        }
        const authorization = request.headers.get("authorization") ?? "";
        const token = /^Bearer ([A-Za-z0-9_-]{43})$/.exec(authorization)?.[1];
        if (!token) {
            return fail(401, "Sign in again to continue.");
        }
        let input;
        if (saveProfile) {
            try {
                input = await readObject(request, 1024);
                if (Object.keys(input).sort().join(",") !== "display_name,user_id"
                    || typeof input.display_name !== "string" || !input.display_name.trim()
                    || Buffer.byteLength(input.display_name) > 64 || /[\u0000-\u001f\u007f]/.test(input.display_name)) {
                    return fail(400, "Use a display name of 1 to 64 UTF-8 bytes without control characters.");
                }
            } catch {
                return fail(400, "The profile update is invalid or too large.");
            }
        }
        try {
            return await withNativeSessionTransaction(pool, token, async (connection, identity) => {
                if (identity.clientId !== clientId) {
                    return fail(401, "Sign in again to continue.");
                }
                if (profile) {
                    if (!identity.scope.split(" ").includes("profile")) {
                        return fail(403, "This desktop session does not have profile access.");
                    }
                    if (saveProfile) {
                        if (url.searchParams.size !== 1 || url.searchParams.get("on_conflict") !== "user_id"
                            || input.user_id !== identity.userId) {
                            return fail(403, "You can only update your own profile.");
                        }
                        const [updated] = await connection.execute(`UPDATE profiles SET display_name=?,updated_at=UTC_TIMESTAMP(6)
                            WHERE user_id=?`, [input.display_name, identity.userId]);
                        if (Number(updated.affectedRows) !== 1) {
                            throw new Error("Account profile is unavailable.");
                        }
                        const [mirrored] = await connection.execute("UPDATE platform_profiles SET display_name=? WHERE user_id=?",
                            [input.display_name, identity.userId]);
                        if (Number(mirrored.affectedRows) !== 1) {
                            throw new Error("Account profile mirror is unavailable.");
                        }
                        return new Response(null, { status: 204, headers });
                    }
                    if (url.searchParams.size !== 3 || url.searchParams.get("select") !== "display_name,avatar_url"
                        || url.searchParams.get("user_id") !== `eq.${identity.userId}` || url.searchParams.get("limit") !== "1") {
                        return fail(403, "You can only read your own profile.");
                    }
                    const [rows] = await connection.execute("SELECT display_name,avatar_url FROM profiles WHERE user_id=?",
                        [identity.userId]);
                    return Response.json(rows, { headers });
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
