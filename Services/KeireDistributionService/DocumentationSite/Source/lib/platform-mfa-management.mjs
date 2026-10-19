import { createHash, randomBytes, randomUUID } from "node:crypto";
import { matchingTotpCounter, verifyImportedPassword } from "./platform-auth-crypto.mjs";
import { openAccountRecord, sealAccountRecord } from "./platform-account-import.mjs";

const enrollmentLifetimeMs = 15 * 60 * 1000;
const maximumFactors = 10;

export async function removeLocalAuthenticator(auth, token, factorId) {
    if (typeof factorId !== "string" || !/^[0-9a-f-]{36}$/i.test(factorId)) {
        return { state: "invalid_request" };
    }
    return await auth.withSessionTransaction(token, async (connection, session) => {
        const [factors] = await connection.execute(`SELECT status FROM platform_mfa_factors
            WHERE id=? AND user_id=? AND factor_type='totp' FOR UPDATE`, [factorId, session.userId]);
        if (factors.length !== 1) {
            return { state: "factor_missing" };
        }
        const verified = factors[0].status === "verified";
        if (verified && session.assurance !== 2) {
            return { state: "mfa_required" };
        }
        if (verified) {
            await connection.execute("DELETE FROM platform_auth_sessions WHERE user_id=?", [session.userId]);
            await connection.execute("UPDATE platform_auth_challenges SET used=TRUE WHERE user_id=?", [session.userId]);
            await connection.execute("UPDATE platform_email_challenges SET used=TRUE WHERE user_id=?", [session.userId]);
            await connection.execute("UPDATE platform_recovery_challenges SET used=TRUE WHERE user_id=?", [session.userId]);
        }
        await connection.execute("DELETE FROM platform_mfa_counters WHERE factor_id=?", [factorId]);
        await connection.execute("DELETE FROM platform_mfa_factors WHERE id=? AND user_id=?", [factorId, session.userId]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,?,'mfa_factor',?,?,'{}')`,
        [session.userId, verified ? "account.authenticator_removed" : "account.authenticator_setup_cancelled", factorId, randomUUID()]);
        return { state: "removed", signedOut: verified };
    }) ?? { state: "invalid_session" };
}

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
        const sealed = sealAccountRecord({ secret, enrollmentExpiresAt: expiresAt, enrollmentAttempts: 0,
            passwordDigest: createHash("sha256").update(accounts[0].password_hash).digest("hex") },
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

export async function confirmLocalAuthenticator(auth, token, factorId, code) {
    if (typeof factorId !== "string" || !/^[0-9a-f-]{36}$/i.test(factorId)) {
        return { state: "invalid_request" };
    }
    return await auth.withSessionTransaction(token, async (connection, session) => {
        const [accounts] = await connection.execute("SELECT * FROM platform_accounts WHERE id=?", [session.userId]);
        const [factors] = await connection.execute(
            "SELECT * FROM platform_mfa_factors WHERE user_id=? FOR UPDATE", [session.userId]);
        if (factors.some((factor) => factor.status === "verified" && factor.factor_type !== "email") && session.assurance !== 2) {
            return { state: "mfa_required" };
        }
        const factor = factors.find((item) => item.id === factorId && item.factor_type === "totp" && item.status === "unverified");
        if (!factor || accounts.length !== 1) {
            return { state: "invalid_enrollment" };
        }
        const enrollment = openAccountRecord(factor.source_sealed, auth.encryptionKey, `mfa:${factorId}`);
        const now = auth.now();
        if (!Number.isSafeInteger(enrollment.enrollmentExpiresAt) || now >= enrollment.enrollmentExpiresAt ||
            !Number.isSafeInteger(enrollment.enrollmentAttempts) || enrollment.enrollmentAttempts < 0 ||
            enrollment.enrollmentAttempts >= 5 || enrollment.passwordDigest !==
                createHash("sha256").update(accounts[0].password_hash ?? "").digest("hex")) {
            return { state: "invalid_enrollment" };
        }
        enrollment.enrollmentAttempts++;
        const counter = matchingTotpCounter(enrollment.secret, code, now);
        await connection.execute("UPDATE platform_mfa_factors SET source_sealed=? WHERE id=?", [
            sealAccountRecord(enrollment, auth.encryptionKey, `mfa:${factorId}`), factorId,
        ]);
        if (counter === null) {
            return { state: "invalid_code" };
        }
        await connection.execute("UPDATE platform_mfa_factors SET status='verified' WHERE id=?", [factorId]);
        await connection.execute("INSERT INTO platform_mfa_counters (factor_id,accepted_counter) VALUES (?,?)", [factorId, counter]);
        await connection.execute("DELETE FROM platform_auth_sessions WHERE user_id=?", [session.userId]);
        await connection.execute("UPDATE platform_auth_challenges SET used=TRUE WHERE user_id=?", [session.userId]);
        await connection.execute("UPDATE platform_email_challenges SET used=TRUE WHERE user_id=?", [session.userId]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,'account.authenticator_added','mfa_factor',?,?,'{}')`,
        [session.userId, factorId, randomUUID()]);
        return auth.issueSession(connection, accounts[0], now, factorId);
    }) ?? { state: "invalid_session" };
}
