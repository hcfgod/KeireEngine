import { randomUUID } from "node:crypto";
import { sealAccountRecord, openAccountRecord } from "./platform-account-import.mjs";
import { accountAvailable } from "./platform-local-auth.mjs";
import { platformTransaction } from "./platform-mysql.mjs";
import { reserveEmailSend } from "./platform-email-limits.mjs";
import { insertRecoveryChallenge, finishRecoveryDelivery, recoveryLifetimeMs } from "./platform-recovery-store.mjs";

const accepted = Object.freeze({ state: "accepted" });

export async function createRecoveryDeliverySchema(pool) {
    await pool.query(`CREATE TABLE platform_recovery_delivery (
        id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
        payload_sealed LONGBLOB NULL,
        state VARCHAR(16) CHARACTER SET ascii NOT NULL DEFAULT 'queued',
        created_ms BIGINT UNSIGNED NOT NULL,
        expires_ms BIGINT UNSIGNED NOT NULL,
        CHECK (state IN ('queued','processing','done','failed')),
        CHECK (expires_ms=created_ms+900000),
        INDEX recovery_delivery_pending (state,created_ms,id)
    ) ENGINE=InnoDB`);
}

/** Queue every syntactically valid address without looking up whether it belongs to an account. */
export async function enqueueRecoveryDelivery(auth, email, clientIdentity) {
    if (typeof email !== "string" || email.length > 254 || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email) ||
        typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 200) {
        return { state: "invalid_request" };
    }
    const emailKey = email.trim().toLowerCase();
    const now = auth.now();
    if (!await auth.reserveAttempt(`recovery-request:${emailKey}`, `recovery-request:${clientIdentity}`, now)) {
        return accepted;
    }
    const id = randomUUID();
    const sealed = sealAccountRecord({ email: emailKey, clientIdentity }, auth.encryptionKey, `recovery-delivery:${id}`);
    await auth.pool.execute(`INSERT INTO platform_recovery_delivery (id,payload_sealed,created_ms,expires_ms)
        VALUES (?,?,?,?)`, [id, sealed, now, now + recoveryLifetimeMs]);
    return accepted;
}

/** One bounded worker operation. Claimed jobs are never retried after uncertain SMTP delivery. */
export async function processRecoveryDelivery(auth, mailer, jobId = null) {
    if (jobId !== null && !/^[a-f0-9-]{36}$/i.test(jobId)) {
        throw new Error("Invalid recovery delivery job.");
    }
    const now = auth.now();
    const scope = jobId === null ? "" : " AND id=?";
    const parameters = jobId === null ? [now] : [now, jobId];
    const job = await platformTransaction(auth.pool, async (connection) => {
        await connection.execute(`UPDATE platform_recovery_delivery SET state='failed',payload_sealed=NULL
            WHERE state IN ('queued','processing') AND expires_ms<=?${scope}`, parameters);
        const [jobs] = await connection.execute(`SELECT id,payload_sealed FROM platform_recovery_delivery
            WHERE state='queued' AND expires_ms>?${scope} ORDER BY created_ms,id LIMIT 1 FOR UPDATE SKIP LOCKED`, parameters);
        if (!jobs.length) {
            return null;
        }
        await connection.execute("UPDATE platform_recovery_delivery SET state='processing' WHERE id=?", [jobs[0].id]);
        return jobs[0];
    });
    if (!job) {
        return false;
    }
    let delivery = null;
    let state = "done";
    try {
        const request = openAccountRecord(job.payload_sealed, auth.encryptionKey, `recovery-delivery:${job.id}`);
        delivery = await platformTransaction(auth.pool, async (connection) => {
            const [accounts] = await connection.execute("SELECT * FROM platform_accounts WHERE email_key=? FOR UPDATE", [request.email]);
            const account = accounts[0];
            const issuedAt = auth.now();
            if (!accountAvailable(account, issuedAt)) {
                return null;
            }
            const [factors] = await connection.execute(
                "SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified' ORDER BY id FOR UPDATE", [account.id]);
            if (factors.some((factor) => !["totp", "email"].includes(factor.factor_type))) {
                return null;
            }
            const reservation = await reserveEmailSend(connection, account.id, request.clientIdentity, issuedAt);
            if (!reservation.allowed) {
                return null;
            }
            const token = await insertRecoveryChallenge(connection, account, factors, issuedAt);
            return { token, recipient: account.email, deliveryId: job.id };
        });
        if (delivery) {
            await mailer.sendRecovery(delivery);
            await finishRecoveryDelivery(auth.pool, delivery.token, true);
        }
    } catch {
        state = "failed";
        if (delivery) {
            await finishRecoveryDelivery(auth.pool, delivery.token, false);
        }
    } finally {
        delivery = null;
        job.payload_sealed.fill(0);
        await auth.pool.execute("UPDATE platform_recovery_delivery SET state=?,payload_sealed=NULL WHERE id=?", [state, job.id]);
    }
    return true;
}
