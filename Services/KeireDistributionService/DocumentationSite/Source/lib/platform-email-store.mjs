import { platformTransaction } from "./platform-mysql.mjs";
import { tokenDigest } from "./platform-auth-crypto.mjs";
import { emailCodeMaximumAttempts, matchesEmailCode } from "./platform-email-code.mjs";

export async function createEmailChallengeSchema(pool) {
    // Explicit migration only. Never create schema from a public request or silently replace existing rows.
    await pool.query(`CREATE TABLE platform_email_challenges (
        token_hash CHAR(64) CHARACTER SET ascii PRIMARY KEY,
        user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        proof CHAR(64) CHARACTER SET ascii NOT NULL,
        issued_ms BIGINT UNSIGNED NOT NULL,
        expires_ms BIGINT UNSIGNED NOT NULL,
        attempts INT UNSIGNED NOT NULL DEFAULT 0,
        delivery_state VARCHAR(16) CHARACTER SET ascii NOT NULL DEFAULT 'pending',
        used BOOLEAN NOT NULL DEFAULT FALSE,
        FOREIGN KEY (user_id) REFERENCES platform_accounts(id),
        CONSTRAINT email_delivery_state CHECK (delivery_state IN ('pending','sent','failed')),
        CONSTRAINT email_valid_window CHECK (expires_ms = issued_ms + 300000),
        INDEX email_challenge_expiry (expires_ms)
    ) ENGINE=InnoDB`);
}

/** Use within the authorized issuance transaction after reserving account/client send limits. */
export async function insertEmailChallenge(connection, userId, record) {
    await connection.execute(`INSERT INTO platform_email_challenges
        (token_hash,user_id,proof,issued_ms,expires_ms) VALUES (?,?,?,?,?)`,
    [record.tokenHash, userId, record.proof, record.issuedAt, record.expiresAt]);
}

/** A failed/uncertain send stays unusable. Never retry an uncertain SMTP send with a new valid challenge. */
export async function finishEmailDelivery(pool, token, sent) {
    const hash = tokenDigest(token);
    if (!hash || typeof sent !== "boolean") {
        throw new Error("Invalid email delivery result.");
    }
    const [result] = await pool.execute(`UPDATE platform_email_challenges SET delivery_state = ?
        WHERE token_hash = ? AND delivery_state = 'pending' AND used = FALSE`, [sent ? "sent" : "failed", hash]);
    return result.affectedRows === 1;
}

/**
 * The callback must recheck account/factor/session authorization under locks and apply the change on connection.
 * Returning null rejects authorization; throwing rolls back both consumption and callback writes.
 * This row budget supplements (does not replace) shared account/client and password-challenge limits.
 */
export async function consumeEmailChallenge(pool, key, context, token, code, now, authorizeAndApply) {
    return platformTransaction(pool, (connection) =>
        consumeEmailChallengeInTransaction(connection, key, context, token, code, now, authorizeAndApply));
}

/** The caller owns the transaction, including shared password-challenge attempt accounting. */
export async function consumeEmailChallengeInTransaction(connection, key, context, token, code, now, authorizeAndApply) {
    const hash = tokenDigest(token);
    if (!hash || typeof authorizeAndApply !== "function") {
        return null;
    }
    const [rows] = await connection.execute(
        "SELECT * FROM platform_email_challenges WHERE token_hash = ? FOR UPDATE", [hash]);
    const row = rows[0];
    if (!row || row.used || row.delivery_state !== "sent" || row.user_id !== context.userId ||
        Number(row.expires_ms) <= now || row.attempts >= emailCodeMaximumAttempts) {
        return null;
    }
    await connection.execute("UPDATE platform_email_challenges SET attempts = attempts + 1 WHERE token_hash = ?", [hash]);
    const record = { tokenHash: row.token_hash, proof: row.proof,
        issuedAt: Number(row.issued_ms), expiresAt: Number(row.expires_ms) };
    if (!matchesEmailCode(key, context, record, token, code, now)) {
        return null;
    }
    await connection.query("SAVEPOINT email_authorization");
    const result = await authorizeAndApply(connection);
    if (result === null || result === undefined) {
        await connection.query("ROLLBACK TO SAVEPOINT email_authorization");
        return null;
    }
    await connection.execute("UPDATE platform_email_challenges SET used = TRUE WHERE token_hash = ?", [hash]);
    return result;
}
