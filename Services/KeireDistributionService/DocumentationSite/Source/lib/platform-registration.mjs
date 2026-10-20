import { randomUUID } from "node:crypto";
import bcrypt from "bcryptjs";
import { newOpaqueToken, tokenDigest } from "./platform-auth-crypto.mjs";
import { sealAccountRecord } from "./platform-account-import.mjs";

export const registrationLifetimeMs = 30 * 60 * 1000;
const accepted = Object.freeze({ state: "accepted" });

export async function createRegistrationSchema(pool) {
    await pool.query(`CREATE TABLE platform_registration_requests (
        id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
        token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL UNIQUE,
        payload_sealed LONGBLOB NULL,
        delivery_state VARCHAR(16) CHARACTER SET ascii NOT NULL DEFAULT 'queued',
        attempts INT UNSIGNED NOT NULL DEFAULT 0,
        used BOOLEAN NOT NULL DEFAULT FALSE,
        created_ms BIGINT UNSIGNED NOT NULL,
        expires_ms BIGINT UNSIGNED NOT NULL,
        CHECK (delivery_state IN ('queued','processing','sent','failed')),
        CHECK (expires_ms=created_ms+1800000),
        INDEX registration_pending (delivery_state,created_ms,id)
    ) ENGINE=InnoDB`);
}

export function registrationDetails(input) {
    if (!input || input.accepted !== true || typeof input.email !== "string" ||
        typeof input.displayName !== "string" || typeof input.password !== "string") {
        return null;
    }
    const email = input.email.trim().toLowerCase();
    const displayName = input.displayName.trim();
    if (email.length > 254 || !/^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?$/.test(email) ||
        !displayName || displayName.length > 64 || /[\u0000-\u001f\u007f]/.test(displayName) ||
        input.password.length < 10 || input.password.length > 256 || bcrypt.truncates(input.password)) {
        return null;
    }
    return { email, displayName, password: input.password };
}

/** No account lookup or creation occurs before the address receives and confirms its link. */
export async function enqueueRegistration(authentication, input, clientIdentity) {
    const details = registrationDetails(input);
    if (!details || typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 200) {
        return { state: "invalid_request" };
    }
    const now = authentication.now();
    if (!Number.isSafeInteger(now) || now < 0 || !Number.isSafeInteger(now + registrationLifetimeMs)) {
        throw new Error("Registration requires a valid server clock.");
    }
    const allowed = await authentication.reserveAttempt(
        `registration:${details.email}`, `registration:${clientIdentity}`, now);
    if (!allowed) {
        return accepted;
    }
    const id = randomUUID();
    const token = newOpaqueToken();
    const passwordHash = await bcrypt.hash(details.password, 12);
    const payload = sealAccountRecord({
        email: details.email,
        displayName: details.displayName,
        passwordHash,
        token,
        clientIdentity,
        termsAcceptedAt: now,
    }, authentication.encryptionKey, `registration:${id}`);
    try {
        await authentication.pool.execute(`INSERT INTO platform_registration_requests
            (id,token_hash,payload_sealed,created_ms,expires_ms) VALUES (?,?,?,?,?)`,
        [id, tokenDigest(token), payload, now, now + registrationLifetimeMs]);
    } finally {
        payload.fill(0);
    }
    return accepted;
}
