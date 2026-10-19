import assert from "node:assert/strict";
import { createHmac, randomUUID } from "node:crypto";
import { beginLocalAuthenticator, confirmLocalAuthenticator } from "../Source/lib/platform-mfa-management.mjs";
import { openAccountRecord, sealAccountRecord } from "../Source/lib/platform-account-import.mjs";
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
            issueSession: (...arguments_) => authentication.issueSession(...arguments_),
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
        assert.equal((await confirmLocalAuthenticator(auth, "fixture", randomUUID(), "123456")).state, "invalid_enrollment");
        for (let attempt = 0; attempt < 5; attempt++) {
            assert.equal((await confirmLocalAuthenticator(auth, "fixture", second.factorId, "bad")).state, "invalid_code");
        }
        assert.equal((await confirmLocalAuthenticator(auth, "fixture", second.factorId, "bad")).state, "invalid_enrollment");
        const enrollment = openAccountRecord(rows[0].source_sealed, auth.encryptionKey, `mfa:${first.factorId}`);
        auth.now = () => first.expiresAt;
        assert.equal((await confirmLocalAuthenticator(auth, "fixture", first.factorId, "123456")).state, "invalid_enrollment");
        auth.now = () => authentication.now();
        assurance = 1;
        assert.equal((await confirmLocalAuthenticator(auth, "fixture", first.factorId, "123456")).state, "mfa_required");
        assurance = 2;
        await connection.execute("UPDATE platform_mfa_factors SET source_sealed=? WHERE id=?", [
            sealAccountRecord({ ...enrollment, passwordDigest: "changed" }, auth.encryptionKey, `mfa:${first.factorId}`), first.factorId,
        ]);
        assert.equal((await confirmLocalAuthenticator(auth, "fixture", first.factorId, "123456")).state, "invalid_enrollment");
        checks += 3;
        enrollment.secret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
        await connection.execute("UPDATE platform_mfa_factors SET source_sealed=? WHERE id=?", [
            sealAccountRecord(enrollment, auth.encryptionKey, `mfa:${first.factorId}`), first.factorId,
        ]);
        const message = Buffer.alloc(8);
        message.writeBigUInt64BE(BigInt(Math.floor(auth.now() / 30000)));
        const digest = createHmac("sha1", Buffer.from("12345678901234567890")).update(message).digest();
        const offset = digest[digest.length - 1] & 15;
        const code = String((digest.readUInt32BE(offset) & 0x7fffffff) % 1000000).padStart(6, "0");
        const confirmed = await confirmLocalAuthenticator(auth, "fixture", first.factorId, code);
        assert.equal(confirmed.state, "authenticated");
        assert.equal(confirmed.assurance, 2);
        assert.equal((await confirmLocalAuthenticator(auth, "fixture", first.factorId, code)).state, "invalid_enrollment");
        const [sessions] = await connection.execute("SELECT factor_id FROM platform_auth_sessions WHERE user_id=?", [userId]);
        assert.deepEqual(sessions, [{ factor_id: first.factorId }]);
        const [counters] = await connection.execute("SELECT accepted_counter FROM platform_mfa_counters WHERE factor_id=?", [first.factorId]);
        assert.equal(Number(counters[0].accepted_counter), Math.floor(auth.now() / 30000));
        checks += 12;
        throw rollback;
    }), (error) => error === rollback);
    return checks;
}
