import { randomUUID } from "node:crypto";
import { openAccountRecord, sealAccountRecord, mysqlUtcTimestamp } from "./platform-account-import.mjs";
import { tokenDigest, verifyImportedPassword } from "./platform-auth-crypto.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

const invalid = Object.freeze({ state: "invalid_confirmation" });
const maximumAttempts = 5;

export async function registrationStatus(authentication, token) {
    const digest = tokenDigest(token);
    if (!digest) {
        return null;
    }
    const [rows] = await authentication.pool.execute(`SELECT delivery_state,used,attempts,created_ms,expires_ms
        FROM platform_registration_requests WHERE token_hash=?`, [digest]);
    const request = rows[0];
    const now = authentication.now();
    if (!request || request.used || request.delivery_state !== "sent" ||
        Number(request.attempts) >= maximumAttempts || !Number.isSafeInteger(now) ||
        now < Number(request.created_ms) || now >= Number(request.expires_ms)) {
        return null;
    }
    return { expiresAt: Number(request.expires_ms) };
}

async function retireRequest(connection, id) {
    await connection.execute(
        "UPDATE platform_registration_requests SET used=TRUE,payload_sealed=NULL WHERE id=?", [id]);
}

async function insertVerifiedAccount(connection, pending, encryptionKey, now) {
    const userId = randomUUID();
    const timestamp = mysqlUtcTimestamp(new Date(now).toISOString());
    const userMetadata = { display_name: pending.displayName, email_verified: true };
    const accountSource = sealAccountRecord({
        origin: "local_registration",
        termsAcceptedAt: pending.termsAcceptedAt,
    }, encryptionKey, `account:${userId}`);
    try {
        await connection.execute(`INSERT INTO platform_accounts
            (id,email,email_key,password_hash,email_confirmed_at,created_at,updated_at,app_metadata,user_metadata,source_sealed)
            VALUES (?,?,?,?,?,?,?, ?,?,?)`,
        [userId, pending.email, pending.email, pending.passwordHash, timestamp, timestamp, timestamp,
            JSON.stringify({ provider: "email", providers: ["email"] }), JSON.stringify(userMetadata), accountSource]);
    } catch (error) {
        if (error?.code === "ER_DUP_ENTRY") {
            return null;
        }
        throw error;
    } finally {
        accountSource.fill(0);
    }
    const identityId = randomUUID();
    const identityData = { sub: userId, email: pending.email, email_verified: true };
    await connection.execute(`INSERT INTO platform_identities
        (id,user_id,provider,provider_id,identity_data,created_at,updated_at,source_record)
        VALUES (?,?,'email',?,?,?,?,?)`,
    [identityId, userId, userId, JSON.stringify(identityData), timestamp, timestamp,
        JSON.stringify({ origin: "local_registration" })]);
    await connection.execute(`INSERT INTO platform_profiles (user_id,display_name,source_record) VALUES (?,?,?)`,
    [userId, pending.displayName, JSON.stringify({ user_id: userId, display_name: pending.displayName })]);
    await connection.execute(`INSERT INTO platform_audit_events
        (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
        VALUES (?, ?,NULL,'account.registered','account',?,?,?)`,
    [timestamp, userId, userId, randomUUID(), JSON.stringify({ termsAcceptedAt: pending.termsAcceptedAt })]);
    return userId;
}

/** Confirmation creates no login session and never replaces credentials on an existing account. */
export async function confirmRegistration(authentication, token, password, clientIdentity) {
    const digest = tokenDigest(token);
    if (!digest || typeof password !== "string" || !password || password.length > 256 ||
        typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 200) {
        return invalid;
    }
    const allowed = await authentication.reserveAttempt(
        `registration-confirm:${digest}`, `registration-confirm:${clientIdentity}`, authentication.now());
    if (!allowed) {
        return { state: "rate_limited" };
    }
    return platformTransaction(authentication.pool, async (connection) => {
        const [rows] = await connection.execute(
            "SELECT * FROM platform_registration_requests WHERE token_hash=? FOR UPDATE", [digest]);
        const request = rows[0];
        if (!request || request.used || request.delivery_state !== "sent") {
            return invalid;
        }
        const now = authentication.now();
        if (!Number.isSafeInteger(now) || now < Number(request.created_ms) ||
            now >= Number(request.expires_ms) || Number(request.attempts) >= maximumAttempts) {
            await retireRequest(connection, request.id);
            return invalid;
        }
        await connection.execute("UPDATE platform_registration_requests SET attempts=attempts+1 WHERE id=?", [request.id]);
        let pending;
        try {
            pending = openAccountRecord(request.payload_sealed, authentication.encryptionKey, `registration:${request.id}`);
        } finally {
            request.payload_sealed?.fill(0);
        }
        if (!await verifyImportedPassword(password, pending.passwordHash)) {
            if (Number(request.attempts) + 1 >= maximumAttempts) {
                await retireRequest(connection, request.id);
            }
            return invalid;
        }
        const userId = await insertVerifiedAccount(connection, pending, authentication.encryptionKey, now);
        await retireRequest(connection, request.id);
        return userId ? { state: "confirmed" } : invalid;
    });
}
