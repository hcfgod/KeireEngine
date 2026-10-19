import assert from "node:assert/strict";
import { createHash, createHmac, randomUUID } from "node:crypto";
import { tokenDigest, verifyImportedPassword } from "../Source/lib/platform-auth-crypto.mjs";
import { insertRecoveryChallenge, finishRecoveryDelivery } from "../Source/lib/platform-recovery-store.mjs";
import { replaceLocalRecoveredPassword } from "../Source/lib/platform-password-recovery.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

export async function verifyPasswordReplacement(pool, auth, userId, factorId, now) {
    const [accounts] = await pool.execute("SELECT * FROM platform_accounts WHERE id=?", [userId]);
    const [factors] = await pool.execute("SELECT * FROM platform_mfa_factors WHERE user_id=?", [userId]);
    const [counters] = await pool.execute("SELECT accepted_counter FROM platform_mfa_counters WHERE factor_id=?", [factorId]);
    const account = accounts[0];
    const client = `recovery-qa-${randomUUID()}`;
    const tokens = [];
    const password = `Replacement-${randomUUID()}`;
    let session;
    try {
        await pool.execute("DELETE FROM platform_mfa_counters WHERE factor_id=?", [factorId]);
        session = await platformTransaction(pool, (connection) => auth.issueSession(connection, account, now, factorId));
        for (let index = 0; index < 2; index++) {
            const token = await platformTransaction(pool, (connection) => insertRecoveryChallenge(connection, account, factors, now));
            tokens.push(token);
            await finishRecoveryDelivery(pool, token, true);
        }
        const replace = (id, code) => replaceLocalRecoveredPassword(auth, tokens[0], password, id, code, client);
        assert.equal((await replace(null, null)).state, "invalid_recovery");
        assert.equal((await replace(factorId, "invalid")).state, "invalid_recovery");
        const counter = Buffer.alloc(8);
        counter.writeBigUInt64BE(BigInt(Math.floor(now / 30000)));
        const digest = createHmac("sha1", Buffer.from("12345678901234567890")).update(counter).digest();
        const offset = digest[digest.length - 1] & 15;
        const code = String((digest.readUInt32BE(offset) & 0x7fffffff) % 1000000).padStart(6, "0");
        assert.equal((await replace(factorId, code)).state, "updated");
        assert.equal(await auth.session(session.token), null);
        const [updated] = await pool.execute("SELECT password_hash FROM platform_accounts WHERE id=?", [userId]);
        assert.equal(await verifyImportedPassword(password, updated[0].password_hash), true);
        assert.equal((await replace(factorId, code)).state, "invalid_recovery");
        assert.equal((await replaceLocalRecoveredPassword(auth, tokens[1], password, factorId, code, client)).state, "invalid_recovery");
        const [remainingFactors] = await pool.execute("SELECT * FROM platform_mfa_factors WHERE user_id=?", [userId]);
        assert.deepEqual(remainingFactors, factors);
        const [events] = await pool.execute("SELECT id FROM platform_audit_events WHERE actor_user_id=? AND action='account.password_recovered'", [userId]);
        assert.equal(events.length, 1);
        return 9;
    } finally {
        await pool.execute("UPDATE platform_accounts SET password_hash=? WHERE id=?", [account.password_hash, userId]);
        await pool.execute("DELETE FROM platform_mfa_counters WHERE factor_id=?", [factorId]);
        if (counters.length) {
            await pool.execute("INSERT INTO platform_mfa_counters (factor_id,accepted_counter) VALUES (?,?)", [factorId, counters[0].accepted_counter]);
        }
        if (session) {
            await auth.signOut(session.token);
        }
        await pool.execute("DELETE FROM platform_audit_events WHERE actor_user_id=? AND action='account.password_recovered'", [userId]);
        for (const token of tokens) {
            const hash = tokenDigest(token);
            await pool.execute("DELETE FROM platform_recovery_challenges WHERE token_hash=?", [hash]);
            for (const subject of [`email:recovery:${hash}`, `pair:recovery-complete:${client}\0recovery:${hash}`]) {
                await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?", [createHash("sha256").update(subject).digest("hex")]);
            }
        }
        await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?",
            [createHash("sha256").update(`client:recovery-complete:${client}`).digest("hex")]);
    }
}
