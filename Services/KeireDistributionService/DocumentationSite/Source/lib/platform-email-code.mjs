import { createHmac, randomInt, timingSafeEqual } from "node:crypto";
import { newOpaqueToken, tokenDigest } from "./platform-auth-crypto.mjs";

export const emailCodeLifetimeMs = 5 * 60 * 1000;
export const emailCodeMaximumAttempts = 5;

function validContext(context) {
    return context && ["sign_in", "enroll"].includes(context.purpose) &&
        typeof context.userId === "string" && /^[a-f0-9-]{36}$/i.test(context.userId) &&
        typeof context.email === "string" && context.email.length > 0 && context.email.length <= 320 &&
        !/[\r\n]/.test(context.email) &&
        typeof context.passwordDigest === "string" && /^[a-f0-9]{64}$/.test(context.passwordDigest) &&
        typeof context.authorizationDigest === "string" && /^[a-f0-9]{64}$/.test(context.authorizationDigest);
}

function validKey(key) {
    if (!Buffer.isBuffer(key) || key.length !== 32) {
        throw new Error("Email verification requires a protected 32-byte key.");
    }
}

function proof(key, context, tokenHash, issuedAt, expiresAt, code) {
    // Keyed hashing prevents offline enumeration of the small code space after a database-only leak.
    // The authorization digest binds enrollment to its session, or sign-in to its password challenge.
    return createHmac("sha256", key).update(JSON.stringify([
        "Keire.EmailVerification.v1", context.purpose, context.userId, context.email,
        context.passwordDigest, context.authorizationDigest, tokenHash, issuedAt, expiresAt, code,
    ])).digest("hex");
}

/** Return the code only to the delivery layer. Persist record, never token or code. */
export function createEmailCode(key, context, now = Date.now()) {
    validKey(key);
    if (!validContext(context) || !Number.isSafeInteger(now) || now < 0 ||
        !Number.isSafeInteger(now + emailCodeLifetimeMs)) {
        throw new Error("Invalid email verification context.");
    }
    const token = newOpaqueToken();
    const code = String(randomInt(0, 100000000)).padStart(8, "0");
    const tokenHash = tokenDigest(token);
    const expiresAt = now + emailCodeLifetimeMs;
    return {
        token,
        code,
        record: {
            tokenHash,
            issuedAt: now,
            expiresAt,
            proof: proof(key, context, tokenHash, now, expiresAt, code),
        },
    };
}

/** Caller must lock the row, enforce the shared attempt limit, and consume it in the session transaction. */
export function matchesEmailCode(key, context, record, token, code, now = Date.now()) {
    validKey(key);
    if (!validContext(context) || typeof code !== "string" || !/^[0-9]{8}$/.test(code) ||
        !Number.isSafeInteger(now) || now < 0 || !record ||
        !Number.isSafeInteger(record.issuedAt) || record.issuedAt < 0 ||
        !Number.isSafeInteger(record.expiresAt) || record.expiresAt - record.issuedAt !== emailCodeLifetimeMs ||
        now < record.issuedAt || now >= record.expiresAt ||
        typeof record.proof !== "string" || !/^[a-f0-9]{64}$/.test(record.proof)) {
        return false;
    }
    const tokenHash = tokenDigest(token);
    if (!tokenHash || tokenHash !== record.tokenHash) {
        return false;
    }
    const expected = proof(key, context, tokenHash, record.issuedAt, record.expiresAt, code);
    return timingSafeEqual(Buffer.from(expected, "hex"), Buffer.from(record.proof, "hex"));
}
