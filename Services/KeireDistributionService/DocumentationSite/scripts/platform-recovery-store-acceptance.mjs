import assert from "node:assert/strict";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";
import { insertRecoveryChallenge, finishRecoveryDelivery, consumeRecoveryChallenge,
    recoveryLifetimeMs, recoveryStatus } from "../Source/lib/platform-recovery-store.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

export async function verifyRecoveryStore(pool, userId, now) {
    const hashes = [];
    let checks = 0;
    const create = async () => platformTransaction(pool, async (connection) => {
        const [accounts] = await connection.execute("SELECT * FROM platform_accounts WHERE id=? FOR UPDATE", [userId]);
        const [factors] = await connection.execute("SELECT * FROM platform_mfa_factors WHERE user_id=? FOR UPDATE", [userId]);
        const token = await insertRecoveryChallenge(connection, accounts[0], factors, now);
        hashes.push(tokenDigest(token));
        return token;
    });
    const consume = (token, action = async () => "applied", time = now) =>
        consumeRecoveryChallenge(pool, token, () => time, action);
    try {
        const pending = await create();
        const statusAuth = { pool, now: () => now };
        assert.equal(await recoveryStatus(statusAuth, pending), null);
        assert.equal(await consume(pending), null);
        assert.equal(await finishRecoveryDelivery(pool, pending, true), true);
        const metadata = await recoveryStatus(statusAuth, pending);
        assert.equal(metadata.expiresAt, now + recoveryLifetimeMs);
        assert.ok(metadata.factors.every((factor) => Object.keys(factor).sort().join(",") === "friendlyName,id"));
        assert.equal(Object.hasOwn(metadata, "userId"), false);
        checks += 4;
        assert.equal(await finishRecoveryDelivery(pool, pending, false), false);
        const simultaneous = await Promise.all([consume(pending), consume(pending)]);
        assert.equal(simultaneous.filter((result) => result === "applied").length, 1);
        assert.equal(await consume(pending), null);
        checks += 5;
        const expired = await create();
        await finishRecoveryDelivery(pool, expired, true);
        assert.equal(await consume(expired, undefined, now + recoveryLifetimeMs), null);
        assert.equal(await consume(expired, undefined, now - 1), null);
        checks += 2;
        const failed = await create();
        await finishRecoveryDelivery(pool, failed, false);
        assert.equal(await consume(failed), null);
        checks++;
        const exhausted = await create();
        await finishRecoveryDelivery(pool, exhausted, true);
        const [before] = await pool.execute("SELECT app_metadata FROM platform_accounts WHERE id=?", [userId]);
        for (let index = 0; index < 5; index++) {
            assert.equal(await consume(exhausted, async (connection) => {
                await connection.execute("UPDATE platform_accounts SET app_metadata=? WHERE id=?",
                    [JSON.stringify({ syntheticRecoveryMutation: true }), userId]);
                return null;
            }), null);
        }
        const [after] = await pool.execute("SELECT app_metadata FROM platform_accounts WHERE id=?", [userId]);
        assert.deepEqual(after, before);
        checks++;
        assert.equal(await consume(exhausted), null);
        checks++;
        const interrupted = await create();
        await finishRecoveryDelivery(pool, interrupted, true);
        const failure = new Error("synthetic reset failure");
        await assert.rejects(consume(interrupted, async () => { throw failure; }), (error) => error === failure);
        assert.equal(await consume(interrupted), "applied");
        checks += 2;
    } finally {
        for (const hash of hashes) {
            await pool.execute("DELETE FROM platform_recovery_challenges WHERE token_hash=?", [hash]);
        }
    }
    return checks;
}
