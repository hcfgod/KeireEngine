import { randomBytes, randomUUID } from "node:crypto";
import { verifyImportedPassword } from "./platform-auth-crypto.mjs";
import { sealAccountRecord } from "./platform-account-import.mjs";

const enrollmentLifetimeMs = 15 * 60 * 1000;
const maximumFactors = 10;

export async function beginLocalAuthenticator(auth, token, password, friendlyName, clientIdentity) {
    const name = typeof friendlyName === "string" ? friendlyName.trim() : "";
    if (name.length < 3 || name.length > 64 || typeof clientIdentity !== "string" ||
        !clientIdentity || clientIdentity.length > 200) {
        return { state: "invalid_request" };
    }
    const current = await auth.session(token);
    if (!current) {
        return { state: "invalid_session" };
    }
    if (!await auth.reserveAttempt(`mfa-enroll:${current.userId}`, `mfa-enroll:${clientIdentity}`, auth.now())) {
        return { state: "rate_limited" };
    }
    return await auth.withSessionTransaction(token, async (connection, session) => {
        const [accounts] = await connection.execute("SELECT email,password_hash FROM platform_accounts WHERE id=?", [session.userId]);
        if (accounts.length !== 1 || !await verifyImportedPassword(password, accounts[0].password_hash)) {
            return { state: "invalid_credentials" };
        }
        const [factors] = await connection.execute(
            "SELECT id,factor_type,status FROM platform_mfa_factors WHERE user_id=? FOR UPDATE", [session.userId]);
        if (factors.some((factor) => factor.status === "verified" && factor.factor_type !== "email") && session.assurance !== 2) {
            return { state: "mfa_required" };
        }
        if (factors.length >= maximumFactors) {
            return { state: "factor_limit" };
        }
        const factorId = randomUUID();
        const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        const entropy = randomBytes(32);
        // Each uniform five-bit symbol contributes five bits: 32 symbols provide a 160-bit secret.
        const secret = Array.from(entropy, (byte) => alphabet[byte & 31]).join("");
        entropy.fill(0);
        const expiresAt = auth.now() + enrollmentLifetimeMs;
        const sealed = sealAccountRecord({ secret, enrollmentExpiresAt: expiresAt, enrollmentAttempts: 0 },
            auth.encryptionKey, `mfa:${factorId}`);
        await connection.execute(`INSERT INTO platform_mfa_factors
            (id,user_id,factor_type,status,friendly_name,source_sealed) VALUES (?,?,'totp','unverified',?,?)`,
        [factorId, session.userId, name, sealed]);
        const issuer = "Kéire Engine";
        const label = encodeURIComponent(`${issuer}:${accounts[0].email}`);
        const parameters = new URLSearchParams({ secret, issuer, algorithm: "SHA1", digits: "6", period: "30" });
        return { state: "pending", factorId, secret, uri: `otpauth://totp/${label}?${parameters}`, expiresAt };
    }) ?? { state: "invalid_session" };
}
