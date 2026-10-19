import { randomUUID } from "node:crypto";
import bcrypt from "bcryptjs";
import { matchingTotpCounter, tokenDigest, verifyImportedPassword } from "./platform-auth-crypto.mjs";
import { openAccountRecord } from "./platform-account-import.mjs";
import { consumeRecoveryChallenge } from "./platform-recovery-store.mjs";

export function validRecoveryPassword(password) {
    return typeof password === "string" && password.length >= 10 && password.length <= 256 &&
        !bcrypt.truncates(password);
}

/** Borrows the authentication service; no recovery token is accepted as an ordinary login session. */
export async function replaceLocalRecoveredPassword(authentication, token, password, factorId, code, clientIdentity) {
    if (!validRecoveryPassword(password)) {
        return { state: "invalid_password" };
    }
    const tokenHash = tokenDigest(token);
    if (!tokenHash || typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 200) {
        return { state: "invalid_recovery" };
    }
    const now = authentication.now();
    if (!await authentication.reserveAttempt(`recovery:${tokenHash}`, `recovery-complete:${clientIdentity}`, now)) {
        return { state: "rate_limited" };
    }
    const result = await consumeRecoveryChallenge(authentication.pool, token, () => authentication.now(),
        async (connection, account, factors, currentTime) => {
            if (factors.some((factor) => !["totp", "email"].includes(factor.factor_type))) {
                return null;
            }
            const authenticators = factors.filter((factor) => factor.factor_type === "totp");
            let acceptedCounter = null;
            if (authenticators.length) {
                const factor = authenticators.find((candidate) => candidate.id === factorId);
                if (!factor) {
                    return null;
                }
                const enrollment = openAccountRecord(factor.source_sealed, authentication.encryptionKey, `mfa:${factor.id}`);
                const [counters] = await connection.execute(
                    "SELECT accepted_counter FROM platform_mfa_counters WHERE factor_id=? FOR UPDATE", [factor.id]);
                acceptedCounter = matchingTotpCounter(enrollment.secret, code, currentTime,
                    counters.length ? Number(counters[0].accepted_counter) : null);
                if (acceptedCounter === null) {
                    return null;
                }
            }
            if (await verifyImportedPassword(password, account.password_hash)) {
                return null;
            }
            const passwordHash = await bcrypt.hash(password, 12);
            await connection.execute("UPDATE platform_accounts SET password_hash=? WHERE id=?", [passwordHash, account.id]);
            if (acceptedCounter !== null) {
                await connection.execute(`INSERT INTO platform_mfa_counters (factor_id,accepted_counter) VALUES (?,?)
                    ON DUPLICATE KEY UPDATE accepted_counter=?`, [factorId, acceptedCounter, acceptedCounter]);
            }
            await connection.execute("DELETE FROM platform_auth_sessions WHERE user_id=?", [account.id]);
            await connection.execute("UPDATE platform_auth_challenges SET used=TRUE WHERE user_id=?", [account.id]);
            await connection.execute("UPDATE platform_email_challenges SET used=TRUE WHERE user_id=?", [account.id]);
            await connection.execute("UPDATE platform_recovery_challenges SET used=TRUE WHERE user_id=?", [account.id]);
            await connection.execute(`INSERT INTO platform_audit_events
                (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
                VALUES (UTC_TIMESTAMP(6),?,NULL,'account.password_recovered','account',?,?,'{}')`,
            [account.id, account.id, randomUUID()]);
            return { state: "updated", signedOut: true };
        });
    return result ?? { state: "invalid_recovery" };
}
