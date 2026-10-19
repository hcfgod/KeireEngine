import { createHash } from "node:crypto";

/** Identity must be derived by the host, never from a submitted header or form field. */
export function emailSendLimits(userId, clientIdentity) {
    if (typeof userId !== "string" || !/^[a-f0-9-]{36}$/i.test(userId) ||
        typeof clientIdentity !== "string" || clientIdentity.length === 0 || clientIdentity.length > 256) {
        throw new Error("Invalid email rate-limit identity.");
    }
    const definitions = [
        { subject: `email-send:cooldown:${userId}`, maximum: 1, windowMs: 60000 },
        { subject: `email-send:account:${userId}`, maximum: 5, windowMs: 900000 },
        { subject: `email-send:client:${clientIdentity}`, maximum: 20, windowMs: 900000 },
    ];
    return definitions.map(({ subject, ...limit }) => ({
        ...limit, hash: createHash("sha256").update(subject).digest("hex"),
    })).sort((left, right) => left.hash.localeCompare(right.hash));
}

/** Call inside the same transaction as authorized challenge creation; retain reservations on SMTP failure. */
export async function reserveEmailSend(connection, userId, clientIdentity, now) {
    if (!Number.isSafeInteger(now) || now < 0 || !Number.isSafeInteger(now + 900000)) {
        throw new Error("Invalid email rate-limit time.");
    }
    const pending = [];
    let retryAfterMs = 0;
    for (const limit of emailSendLimits(userId, clientIdentity)) {
        await connection.execute(`INSERT INTO platform_auth_limits (subject_hash,attempts,window_start)
            VALUES (?,0,?) ON DUPLICATE KEY UPDATE subject_hash = subject_hash`, [limit.hash, now]);
        const [rows] = await connection.execute(
            "SELECT attempts,window_start FROM platform_auth_limits WHERE subject_hash = ? FOR UPDATE", [limit.hash]);
        const start = Number(rows[0].window_start);
        const expired = now - start >= limit.windowMs;
        const attempts = expired ? 0 : Number(rows[0].attempts);
        if (attempts >= limit.maximum) {
            retryAfterMs = Math.max(retryAfterMs, start + limit.windowMs - now);
        }
        pending.push({ hash: limit.hash, attempts: attempts + 1, start: expired ? now : start });
    }
    if (retryAfterMs > 0) {
        return { allowed: false, retryAfterSeconds: Math.ceil(retryAfterMs / 1000) };
    }
    for (const limit of pending) {
        await connection.execute("UPDATE platform_auth_limits SET attempts = ?,window_start = ? WHERE subject_hash = ?",
            [limit.attempts, limit.start, limit.hash]);
    }
    return { allowed: true, retryAfterSeconds: 0 };
}
