import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { LocalEmailAuthentication } from "../Source/lib/platform-email-auth.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";
import { emailSendLimits } from "../Source/lib/platform-email-limits.mjs";

export async function verifyEmailSignIn(pool, auth, account, password, emailFactorId, totpFactorId) {
    const client = `email-signin-qa:${randomUUID()}`;
    const messages = [];
    const parents = [];
    const codes = [];
    const sessions = [];
    const service = new LocalEmailAuthentication(auth, { sendVerification: async (message) => messages.push({ ...message }) });
    const clearSendLimits = async () => {
        for (const limit of emailSendLimits(account.id, client)) {
            await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash = ?", [limit.hash]);
        }
    };
    const begin = async () => {
        // Isolated synthetic account only: reset its send budget between independent test cases.
        await clearSendLimits();
        const parent = await auth.signIn(account.email, password, client);
        assert.equal(parent.state, "mfa_required");
        parents.push(parent.challengeToken);
        const challenge = await service.beginSignIn(parent.challengeToken, emailFactorId, client);
        assert.equal(challenge.state, "code_sent");
        codes.push(challenge.token);
        return { parent: parent.challengeToken, token: challenge.token, code: messages.at(-1).code };
    };
    try {
        const first = await begin();
        assert.equal(messages[0].purpose, "sign_in");
        assert.equal(await auth.session(first.token), null);
        const result = await service.completeSignIn(first.parent, emailFactorId, first.token, first.code);
        assert.equal(result.state, "authenticated");
        sessions.push(result.token);
        assert.equal(result.assurance, 1);
        assert.equal((await auth.session(result.token)).method, "email");
        assert.equal(await auth.pendingChallenge(first.parent), null);
        assert.equal((await service.completeSignIn(first.parent, emailFactorId, first.token, first.code)).state, "invalid_credentials");
        assert.equal((await auth.completeTotp(first.parent, totpFactorId, "000000")).state, "invalid_credentials");

        const exhausted = await begin();
        for (let attempt = 0; attempt < 4; attempt++) {
            assert.equal((await auth.completeTotp(exhausted.parent, totpFactorId, "invalid")).state, "invalid_credentials");
        }
        assert.equal((await service.completeSignIn(exhausted.parent, emailFactorId, exhausted.token, "invalid")).state, "invalid_credentials");
        assert.equal((await service.completeSignIn(exhausted.parent, emailFactorId, exhausted.token, exhausted.code)).state, "invalid_credentials");
        assert.equal(await auth.pendingChallenge(exhausted.parent), null);
        return 11;
    } finally {
        for (const token of sessions) {
            await pool.execute("DELETE FROM platform_auth_sessions WHERE token_hash = ?", [tokenDigest(token)]);
        }
        for (const token of codes) {
            await pool.execute("DELETE FROM platform_email_challenges WHERE token_hash = ?", [tokenDigest(token)]);
        }
        for (const token of parents) {
            await pool.execute("DELETE FROM platform_auth_challenges WHERE token_hash = ?", [tokenDigest(token)]);
        }
        await clearSendLimits();
        for (const subject of [`client:${client}`, `pair:${client}\0${account.email}`]) {
            await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash = ?",
                [createHash("sha256").update(subject).digest("hex")]);
        }
        messages.length = 0;
    }
}
