import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { enqueueRecoveryDelivery, processRecoveryDelivery } from "../Source/lib/platform-recovery-delivery.mjs";
import { emailSendLimits } from "../Source/lib/platform-email-limits.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";

export async function verifyRecoveryDelivery(pool, authentication, userId, now) {
    const [accounts] = await pool.execute("SELECT email FROM platform_accounts WHERE id=?", [userId]);
    const email = accounts[0].email;
    const missingEmail = `missing-${randomUUID()}@invalid.example`;
    const client = `mail-qa-${randomUUID()}`;
    let time = now + 3600000;
    const auth = { pool, encryptionKey: authentication.encryptionKey, now: () => time,
        reserveAttempt: authentication.reserveAttempt.bind(authentication) };
    const jobs = [];
    const tokens = [];
    const enqueue = async (address) => {
        time += 60001;
        assert.deepEqual(await enqueueRecoveryDelivery(auth, address, client), { state: "accepted" });
        const [rows] = await pool.execute("SELECT id FROM platform_recovery_delivery WHERE created_ms=?", [time]);
        assert.equal(rows.length, 1);
        jobs.push(rows[0].id);
        return rows[0].id;
    };
    const capture = { sendRecovery: async (message) => { tokens.push(message.token); assert.equal(message.recipient, email); } };
    try {
        const absent = await enqueue(missingEmail);
        await processRecoveryDelivery(auth, capture, absent);
        assert.equal(tokens.length, 0);
        const present = await enqueue(email);
        const concurrent = await Promise.all([
            processRecoveryDelivery(auth, capture, present), processRecoveryDelivery(auth, capture, present),
        ]);
        assert.equal(concurrent.filter(Boolean).length, 1);
        assert.equal(tokens.length, 1);
        const [success] = await pool.execute("SELECT delivery_state FROM platform_recovery_challenges WHERE token_hash=?", [tokenDigest(tokens[0])]);
        assert.equal(success[0].delivery_state, "sent");
        const failed = await enqueue(email);
        await processRecoveryDelivery(auth, { sendRecovery: async (message) => {
            tokens.push(message.token);
            throw new Error("synthetic SMTP failure");
        } }, failed);
        const [failure] = await pool.execute("SELECT delivery_state FROM platform_recovery_challenges WHERE token_hash=?", [tokenDigest(tokens[1])]);
        assert.equal(failure[0].delivery_state, "failed");
        assert.equal(await processRecoveryDelivery(auth, capture, failed), false);
        for (const id of jobs) {
            const [rows] = await pool.execute("SELECT payload_sealed,state FROM platform_recovery_delivery WHERE id=?", [id]);
            assert.equal(rows[0].payload_sealed, null);
            assert.ok(["done", "failed"].includes(rows[0].state));
        }
        return 9;
    } finally {
        for (const id of jobs) {
            await pool.execute("DELETE FROM platform_recovery_delivery WHERE id=?", [id]);
        }
        for (const token of tokens) {
            await pool.execute("DELETE FROM platform_recovery_challenges WHERE token_hash=?", [tokenDigest(token)]);
        }
        const subjects = [`client:recovery-request:${client}`];
        for (const address of [email, missingEmail]) {
            subjects.push(`email:recovery-request:${address}`, `pair:recovery-request:${client}\0recovery-request:${address}`);
        }
        const hashes = subjects.map((subject) => createHash("sha256").update(subject).digest("hex"));
        hashes.push(...emailSendLimits(userId, client).map((limit) => limit.hash));
        for (const hash of hashes) {
            await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?", [hash]);
        }
    }
}
