import { openAccountRecord, sealAccountRecord } from "./platform-account-import.mjs";
import { tokenDigest } from "./platform-auth-crypto.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

/** Claim once; a crash or uncertain SMTP result must never cause automatic redelivery. */
export async function processRegistrationDelivery(authentication, mailer, jobId = null) {
    if (jobId !== null && !/^[a-f0-9-]{36}$/i.test(jobId)) {
        throw new Error("Invalid registration delivery job.");
    }
    const now = authentication.now();
    const scope = jobId === null ? "" : " AND id=?";
    const parameters = jobId === null ? [now] : [now, jobId];
    const job = await platformTransaction(authentication.pool, async (connection) => {
        await connection.execute(`UPDATE platform_registration_requests
            SET delivery_state='failed',payload_sealed=NULL,used=TRUE
            WHERE used=FALSE AND expires_ms<=?${scope}`, parameters);
        const [rows] = await connection.execute(`SELECT id,token_hash,payload_sealed,created_ms,expires_ms FROM platform_registration_requests
            WHERE delivery_state='queued' AND used=FALSE AND expires_ms>?${scope}
            ORDER BY created_ms,id LIMIT 1 FOR UPDATE SKIP LOCKED`, parameters);
        if (!rows.length) {
            return null;
        }
        await connection.execute("UPDATE platform_registration_requests SET delivery_state='processing' WHERE id=?", [rows[0].id]);
        return rows[0];
    });
    if (!job) {
        return false;
    }
    let confirmationPayload = null;
    let sent = false;
    try {
        const pending = openAccountRecord(job.payload_sealed, authentication.encryptionKey, `registration:${job.id}`);
        if (tokenDigest(pending.token) !== job.token_hash) {
            throw new Error("Invalid registration token binding.");
        }
        const [accounts] = await authentication.pool.execute("SELECT id FROM platform_accounts WHERE email_key=?", [pending.email]);
        const sendAt = authentication.now();
        if (!accounts.length && sendAt >= Number(job.created_ms) && sendAt < Number(job.expires_ms)) {
            confirmationPayload = sealAccountRecord({
                email: pending.email,
                displayName: pending.displayName,
                passwordHash: pending.passwordHash,
                termsAcceptedAt: pending.termsAcceptedAt,
            }, authentication.encryptionKey, `registration:${job.id}`);
            await mailer.sendRegistration({ recipient: pending.email, token: pending.token });
            sent = true;
        }
    } catch {
        // Never expose SMTP details or retry a message that may already have reached its recipient.
        sent = false;
    } finally {
        job.payload_sealed?.fill(0);
        try {
            const finishedAt = authentication.now();
            await authentication.pool.execute(`UPDATE platform_registration_requests
                SET delivery_state=IF(? AND expires_ms>?, 'sent', 'failed'),
                    payload_sealed=IF(? AND expires_ms>?, ?, NULL),
                    used=IF(? AND expires_ms>?, FALSE, TRUE)
                WHERE id=? AND delivery_state='processing' AND used=FALSE`,
            [sent, finishedAt, sent, finishedAt, confirmationPayload, sent, finishedAt, job.id]);
        } finally {
            confirmationPayload?.fill(0);
        }
    }
    return true;
}
