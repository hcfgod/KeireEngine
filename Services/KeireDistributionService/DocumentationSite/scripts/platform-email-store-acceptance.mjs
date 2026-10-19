import assert from "node:assert/strict";
import { createEmailCode } from "../Source/lib/platform-email-code.mjs";
import { consumeEmailChallenge, finishEmailDelivery, insertEmailChallenge } from "../Source/lib/platform-email-store.mjs";

export async function verifyEmailChallengeStore(pool, userId, key) {
    const now = 1700000000000;
    const context = { userId, purpose: "enroll", email: "synthetic@example.com",
        passwordDigest: "a".repeat(64), authorizationDigest: "b".repeat(64) };
    const hashes = [];
    let checks = 0;
    const create = async () => {
        const challenge = createEmailCode(key, context, now);
        await insertEmailChallenge(pool, userId, challenge.record);
        hashes.push(challenge.record.tokenHash);
        return challenge;
    };
    const consume = (challenge, action = async () => "authorized", code = challenge.code, at = now) =>
        consumeEmailChallenge(pool, key, context, challenge.token, code, at, action);
    try {
        const challenge = await create();
        assert.equal(await consume(challenge), null);
        assert.equal(await finishEmailDelivery(pool, challenge.token, true), true);
        assert.equal(await finishEmailDelivery(pool, challenge.token, false), false);
        checks += 3;
        let calls = 0;
        const results = await Promise.all([consume(challenge, async () => ++calls), consume(challenge, async () => ++calls)]);
        assert.equal(calls, 1);
        assert.equal(results.filter((result) => result !== null).length, 1);
        assert.equal(await consume(challenge), null);
        checks += 3;

        const exhausted = await create();
        await finishEmailDelivery(pool, exhausted.token, true);
        for (let attempt = 0; attempt < 5; attempt++) {
            assert.equal(await consume(exhausted, undefined, "invalid"), null);
        }
        assert.equal(await consume(exhausted), null);
        checks++;

        const failed = await create();
        await finishEmailDelivery(pool, failed.token, false);
        assert.equal(await finishEmailDelivery(pool, failed.token, true), false);
        assert.equal(await consume(failed), null);
        checks += 2;

        const rollback = await create();
        await finishEmailDelivery(pool, rollback.token, true);
        await assert.rejects(consume(rollback, async () => { throw new Error("synthetic rollback"); }), /synthetic rollback/);
        assert.equal(await consume(rollback, async (connection) => {
            await connection.execute("UPDATE platform_email_challenges SET proof = ? WHERE token_hash = ?",
                ["f".repeat(64), rollback.record.tokenHash]);
            return null;
        }), null);
        assert.equal(await consume(rollback), "authorized");
        checks += 3;

        const expired = await create();
        await finishEmailDelivery(pool, expired.token, true);
        assert.equal(await consume(expired, undefined, expired.code, now + 300000), null);
        checks++;
        return checks;
    } finally {
        for (const hash of hashes) {
            await pool.execute("DELETE FROM platform_email_challenges WHERE token_hash = ? AND user_id = ?", [hash, userId]);
        }
    }
}
