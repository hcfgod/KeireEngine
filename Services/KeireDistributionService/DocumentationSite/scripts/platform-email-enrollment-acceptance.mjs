import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { LocalEmailAuthentication } from "../Source/lib/platform-email-auth.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";
import { emailSendLimits } from "../Source/lib/platform-email-limits.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { verifyEmailSignIn } from "./platform-email-signin-acceptance.mjs";

export async function verifyEmailEnrollment(pool, auth, userId, password, totpFactorId, now) {
    const client = `email-enrollment-qa:${randomUUID()}`;
    const messages = [];
    const service = new LocalEmailAuthentication(auth, { sendVerification: async (message) => messages.push({ ...message }) });
    const sessions = [];
    let challenge;
    let enrolledFactor;
    let email;
    try {
        const [accounts] = await pool.execute("SELECT * FROM platform_accounts WHERE id = ?", [userId]);
        email = accounts[0].email;
        const strong = await platformTransaction(pool, (connection) => auth.issueSession(connection, accounts[0], now, totpFactorId));
        const weak = await platformTransaction(pool, (connection) => auth.issueSession(connection, accounts[0], now));
        sessions.push(strong.token, weak.token);
        assert.equal((await service.beginEnrollment(weak.token, password, client)).state, "invalid_credentials");
        assert.equal((await service.beginEnrollment(strong.token, "wrong", client)).state, "invalid_credentials");
        assert.equal(messages.length, 0);
        challenge = await service.beginEnrollment(strong.token, password, client);
        assert.equal(challenge.state, "code_sent");
        assert.equal(messages.length, 1);
        assert.equal(messages[0].recipient, email);
        assert.equal(messages[0].purpose, "enroll");
        assert.equal(Object.hasOwn(challenge, "code"), false);
        assert.equal((await service.beginEnrollment(strong.token, password, client)).state, "rate_limited");
        assert.equal(messages.length, 1);
        assert.equal((await service.completeEnrollment(weak.token, challenge.token, messages[0].code)).state, "invalid_credentials");
        assert.equal((await service.completeEnrollment(strong.token, challenge.token, "invalid")).state, "invalid_credentials");
        const enrolled = await service.completeEnrollment(strong.token, challenge.token, messages[0].code);
        enrolledFactor = enrolled.factorId;
        assert.equal(enrolled.state, "enrolled");
        assert.equal((await service.completeEnrollment(strong.token, challenge.token, messages[0].code)).state, "invalid_credentials");
        assert.equal((await auth.session(strong.token)).assurance, 2);
        const [factors] = await pool.execute("SELECT id,factor_type FROM platform_mfa_factors WHERE user_id = ? AND status = 'verified'", [userId]);
        assert.equal(factors.filter((factor) => factor.factor_type === "email").length, 1);
        assert.ok(factors.some((factor) => factor.id === totpFactorId));
        return 17 + await verifyEmailSignIn(pool, auth, accounts[0], password, enrolledFactor, totpFactorId);
    } finally {
        for (const token of sessions) {
            await pool.execute("DELETE FROM platform_auth_sessions WHERE token_hash = ?", [tokenDigest(token)]);
        }
        if (challenge?.token) {
            await pool.execute("DELETE FROM platform_email_challenges WHERE token_hash = ? AND user_id = ?", [tokenDigest(challenge.token), userId]);
        }
        if (enrolledFactor) {
            await pool.execute("DELETE FROM platform_mfa_factors WHERE id = ? AND user_id = ?", [enrolledFactor, userId]);
        }
        for (const limit of emailSendLimits(userId, client)) {
            await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash = ?", [limit.hash]);
        }
        for (const subject of [`client:enroll:${client}`, `pair:enroll:${client}\0${email}`]) {
            await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash = ?",
                [createHash("sha256").update(subject).digest("hex")]);
        }
        messages.length = 0;
    }
}
