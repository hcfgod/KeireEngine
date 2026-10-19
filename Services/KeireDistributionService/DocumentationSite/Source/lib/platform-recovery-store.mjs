import { createHash } from "node:crypto";
import { newOpaqueToken, tokenDigest } from "./platform-auth-crypto.mjs";
import { accountAvailable } from "./platform-local-auth.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

export const recoveryLifetimeMs = 15 * 60 * 1000;
const maximumRecoveryAttempts = 5;

/** Changes to any enrolled factor invalidate pending recovery, including replacement of its encrypted secret. */
export function recoveryBinding(account, factors) {
    const enrolled = factors.filter((factor) => factor.status === "verified")
        .map((factor) => [factor.id, factor.factor_type,
            createHash("sha256").update(factor.source_sealed).digest("hex")])
        .sort((left, right) => left[0].localeCompare(right[0]));
    return createHash("sha256").update(JSON.stringify([
        "Keire.PasswordRecovery.v1", account.id, account.email, account.password_hash, enrolled,
    ])).digest("hex");
}

export async function createRecoverySchema(pool) {
    await pool.query(`CREATE TABLE platform_recovery_challenges (
        token_hash CHAR(64) CHARACTER SET ascii PRIMARY KEY,
        user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        binding_hash CHAR(64) CHARACTER SET ascii NOT NULL,
        issued_ms BIGINT UNSIGNED NOT NULL,
        expires_ms BIGINT UNSIGNED NOT NULL,
        attempts INT UNSIGNED NOT NULL DEFAULT 0,
        delivery_state VARCHAR(16) CHARACTER SET ascii NOT NULL DEFAULT 'pending',
        used BOOLEAN NOT NULL DEFAULT FALSE,
        FOREIGN KEY (user_id) REFERENCES platform_accounts(id),
        CHECK (delivery_state IN ('pending','sent','failed')),
        CHECK (expires_ms = issued_ms + 900000),
        INDEX recovery_expiry (expires_ms)
    ) ENGINE=InnoDB`);
}

/** Caller must lock the account/factors and reserve delivery limits in this transaction. */
export async function insertRecoveryChallenge(connection, account, factors, now) {
    if (!Number.isSafeInteger(now) || now < 0 || !Number.isSafeInteger(now + recoveryLifetimeMs) ||
        !accountAvailable(account, now)) {
        throw new Error("Invalid password recovery context.");
    }
    const token = newOpaqueToken();
    await connection.execute(`INSERT INTO platform_recovery_challenges
        (token_hash,user_id,binding_hash,issued_ms,expires_ms) VALUES (?,?,?,?,?)`,
    [tokenDigest(token), account.id, recoveryBinding(account, factors), now, now + recoveryLifetimeMs]);
    return token;
}

export async function finishRecoveryDelivery(pool, token, sent) {
    const hash = tokenDigest(token);
    if (!hash || typeof sent !== "boolean") {
        throw new Error("Invalid recovery delivery result.");
    }
    const [result] = await pool.execute(`UPDATE platform_recovery_challenges SET delivery_state=?
        WHERE token_hash=? AND delivery_state='pending' AND used=FALSE`, [sent ? "sent" : "failed", hash]);
    return result.affectedRows === 1;
}

/** Callback must verify required MFA and replace the password on this connection; null rejects and spends one attempt. */
export async function consumeRecoveryChallenge(pool, token, clock, authorizeAndApply) {
    const hash = tokenDigest(token);
    if (!hash) {
        return null;
    }
    return platformTransaction(pool, async (connection) => {
        const [rows] = await connection.execute(
            "SELECT * FROM platform_recovery_challenges WHERE token_hash=? FOR UPDATE", [hash]);
        const row = rows[0];
        if (!row || row.used || row.delivery_state !== "sent" || row.attempts >= maximumRecoveryAttempts) {
            return null;
        }
        const [accounts] = await connection.execute("SELECT * FROM platform_accounts WHERE id=? FOR UPDATE", [row.user_id]);
        const [factors] = await connection.execute(
            "SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified' ORDER BY id FOR UPDATE", [row.user_id]);
        const now = clock();
        if (!Number.isSafeInteger(now) || now < Number(row.issued_ms) || now >= Number(row.expires_ms) ||
            !accountAvailable(accounts[0], now) || recoveryBinding(accounts[0], factors) !== row.binding_hash) {
            return null;
        }
        await connection.execute("UPDATE platform_recovery_challenges SET attempts=attempts+1 WHERE token_hash=?", [hash]);
        await connection.query("SAVEPOINT recovery_authorization");
        const result = await authorizeAndApply(connection, accounts[0], factors, now);
        if (result === null || result === undefined) {
            await connection.query("ROLLBACK TO SAVEPOINT recovery_authorization");
            return null;
        }
        await connection.execute("UPDATE platform_recovery_challenges SET used=TRUE WHERE token_hash=?", [hash]);
        return result;
    });
}
