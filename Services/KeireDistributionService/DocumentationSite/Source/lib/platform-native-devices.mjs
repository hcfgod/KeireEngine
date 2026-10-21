import { randomUUID } from "node:crypto";
import { withNativeSessionTransaction } from "./platform-native-sessions.mjs";
import { LocalClaimError } from "./platform-claims.mjs";

/** Register only the authenticated native session; imported Supabase identifiers are never reused. */
export async function registerNativeDevice(pool, accessToken, clientId, deviceName) {
    if (typeof deviceName !== "string" || !deviceName.trim() || [...deviceName.trim()].length > 128
        || /[\u0000-\u001f\u007f]/.test(deviceName)) {
        throw new LocalClaimError(400, "account.invalid_device_name", "Use a device name of 1 to 128 characters.");
    }
    const result = await withNativeSessionTransaction(pool, accessToken, async (connection, identity) => {
        if (identity.clientId !== clientId) {
            throw new LocalClaimError(403, "account.invalid_client", "Sign in again from the registered Kéire Hub.");
        }
        const [[flag]] = await connection.execute(
            "SELECT enabled FROM platform_feature_flags WHERE `key`='hub_oauth_sso_enabled' FOR SHARE");
        if (Number(flag?.enabled) !== 1) {
            throw new LocalClaimError(503, "account.hub_oauth_disabled", "Hub account access is temporarily unavailable.");
        }
        const binding = `mysql:${identity.sessionId}`;
        const [[existing]] = await connection.execute(
            "SELECT id,user_id,client_type,revoked_at FROM oauth_device_sessions WHERE oauth_session_id=? FOR UPDATE", [binding]);
        if (existing) {
            if (existing.user_id !== identity.userId || existing.client_type !== "hub" || existing.revoked_at !== null) {
                throw new LocalClaimError(403, "account.device_revoked", "This device session was revoked. Sign in again.");
            }
            await connection.execute(`UPDATE oauth_device_sessions SET device_name=?,last_used_at=UTC_TIMESTAMP(6)
                WHERE id=?`, [deviceName.trim(), existing.id]);
            return { deviceSessionId: existing.id, oauthSessionId: identity.sessionId };
        }
        const id = randomUUID();
        await connection.execute(`INSERT INTO oauth_device_sessions
            (id,user_id,oauth_session_id,client_type,device_name,created_at,last_used_at)
            VALUES (?,?,?,'hub',?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [id, identity.userId, binding, deviceName.trim()]);
        return { deviceSessionId: id, oauthSessionId: identity.sessionId };
    });
    if (!result) {
        throw new LocalClaimError(401, "account.sign_in_required", "Sign in again to register this device.");
    }
    return result;
}
