import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { emailSendLimits, reserveEmailSend } from "../Source/lib/platform-email-limits.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

export async function verifyEmailSendLimits(pool) {
    const userId = randomUUID();
    const client = `email-qa:${randomUUID()}`;
    const hashes = new Set();
    const now = 1700000000000;
    let checks = 0;
    const reserve = (id, identity, timestamp) => {
        for (const limit of emailSendLimits(id, identity)) {
            hashes.add(limit.hash);
        }
        return platformTransaction(pool, (connection) => reserveEmailSend(connection, id, identity, timestamp));
    };
    try {
        const results = await Promise.all(Array.from({ length: 4 }, () => reserve(userId, client, now)));
        assert.equal(results.filter((result) => result.allowed).length, 1);
        assert.ok(results.filter((result) => !result.allowed).every((result) => result.retryAfterSeconds === 60));
        checks += 2;
        assert.equal((await reserve(userId, client, now + 59999)).allowed, false);
        for (let minute = 1; minute < 5; minute++) {
            assert.equal((await reserve(userId, client, now + minute * 60000)).allowed, true);
        }
        assert.deepEqual(await reserve(userId, client, now + 300000), { allowed: false, retryAfterSeconds: 600 });
        // Changing the client cannot evade the account budget.
        assert.equal((await reserve(userId, client + ":other", now + 300000)).allowed, false);
        checks += 3;
        for (let account = 0; account < 15; account++) {
            assert.equal((await reserve(randomUUID(), client, now + 300000)).allowed, true);
        }
        assert.equal((await reserve(randomUUID(), client, now + 300000)).allowed, false);
        assert.equal((await reserve(userId, client, now + 900000)).allowed, true);
        checks += 2;
        return checks;
    } finally {
        for (const hash of hashes) {
            await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash = ?", [hash]);
        }
    }
}
