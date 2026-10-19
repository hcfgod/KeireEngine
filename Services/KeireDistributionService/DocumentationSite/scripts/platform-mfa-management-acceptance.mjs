import assert from "node:assert/strict";
import { beginLocalAuthenticator } from "../Source/lib/platform-mfa-management.mjs";
import { openAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

export async function verifyAuthenticatorEnrollment(pool, authentication, userId, password) {
    const rollback = new Error("rollback authenticator enrollment");
    let checks = 0;
    await assert.rejects(platformTransaction(pool, async (connection) => {
        let assurance = 2;
        const auth = {
            encryptionKey: authentication.encryptionKey,
            now: () => authentication.now(),
            session: async () => ({ userId, assurance }),
            reserveAttempt: async () => true,
            withSessionTransaction: async (_token, action) => action(connection, { userId, assurance }),
        };
        const enroll = (suppliedPassword = password) => beginLocalAuthenticator(auth, "fixture", suppliedPassword, "QA new authenticator", "qa");
        assert.equal((await enroll("wrong")).state, "invalid_credentials");
        assurance = 1;
        assert.equal((await enroll()).state, "mfa_required");
        assurance = 2;
        const first = await enroll();
        const second = await enroll();
        assert.equal(first.state, "pending");
        assert.match(first.secret, /^[A-Z2-7]{32}$/);
        assert.notEqual(first.secret, second.secret);
        assert.equal(new URL(first.uri).searchParams.get("secret"), first.secret);
        const [rows] = await connection.execute("SELECT status,source_sealed FROM platform_mfa_factors WHERE id=?", [first.factorId]);
        assert.equal(rows[0].status, "unverified");
        assert.equal(openAccountRecord(rows[0].source_sealed, auth.encryptionKey, `mfa:${first.factorId}`).secret, first.secret);
        assert.equal(first.expiresAt - auth.now(), 15 * 60 * 1000);
        checks += 9;
        throw rollback;
    }), (error) => error === rollback);
    return checks;
}
