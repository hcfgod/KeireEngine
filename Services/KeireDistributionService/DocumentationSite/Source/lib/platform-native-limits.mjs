import { createHash } from "node:crypto";
import { platformTransaction } from "./platform-mysql.mjs";
import { NativeOAuthError } from "./platform-native-oauth-policy.mjs";

const limits = Object.freeze({
    authorize: { maximum: 20, windowMs: 600000 },
    token: { maximum: 60, windowMs: 60000 },
});

/** clientIdentity must come from the trusted host/proxy, never a request-selected header. */
export async function reserveNativeRequest(pool, clientIdentity, operation, now = Date.now()) {
    if (typeof clientIdentity !== "string" || clientIdentity.length < 1 || clientIdentity.length > 256
        || !Object.hasOwn(limits, operation) || !Number.isSafeInteger(now) || now < 0) {
        throw new NativeOAuthError("invalid_request", "The request context is unavailable.");
    }
    const subject = createHash("sha256")
        .update(`Keire.NativeOAuth.Rate.v1\0${operation}\0${clientIdentity}`).digest("hex");
    const limit = limits[operation];
    const accepted = await platformTransaction(pool, async (connection) => {
        await connection.execute(`INSERT INTO platform_auth_limits(subject_hash,attempts,window_start)
            VALUES (?,0,?) ON DUPLICATE KEY UPDATE subject_hash=subject_hash`, [subject, now]);
        const [[row]] = await connection.execute(
            "SELECT attempts,window_start FROM platform_auth_limits WHERE subject_hash=? FOR UPDATE", [subject]);
        const elapsed = now - Number(row.window_start);
        if (elapsed < 0) {
            return false;
        }
        const reset = elapsed >= limit.windowMs;
        const attempts = reset ? 0 : Number(row.attempts);
        if (attempts >= limit.maximum) {
            return false;
        }
        await connection.execute("UPDATE platform_auth_limits SET attempts=?,window_start=? WHERE subject_hash=?",
            [attempts + 1, reset ? now : row.window_start, subject]);
        return true;
    });
    if (!accepted) {
        throw new NativeOAuthError("slow_down", "Too many desktop sign-in requests. Please wait and try again.", 429);
    }
}
