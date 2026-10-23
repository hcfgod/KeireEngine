import assert from "node:assert/strict";
import { randomInt, randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { LocalPasswordAuthentication } from "../Source/lib/platform-local-auth.mjs";
import { sealAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { beginGitHubAuthorization, consumeGitHubAuthorization, completeGitHubIdentity }
    from "../Source/lib/platform-github-authorization.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";

/** Uses real MySQL constraints and authentication, but every fixture and audit event rolls back. No GitHub network calls. */
export async function verifyGitHubAuthentication(pool, encryptionKey) {
    let checks = 0;
    const rollback = new Error("rollback GitHub acceptance fixtures");
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const transactionalPool = {
            execute: connection.execute.bind(connection),
            getConnection: async () => ({ execute: connection.execute.bind(connection),
                beginTransaction: async () => {}, commit: async () => {}, rollback: async () => {},
                release: () => {}, destroy: () => {} }),
        };
        let now = Date.now();
        const authentication = await LocalPasswordAuthentication.create(transactionalPool, encryptionKey, () => now);
        try {
            const userId = randomUUID();
            const email = `github-qa-${userId}@invalid.example`;
            await connection.execute(`INSERT INTO platform_accounts
                (id,email,email_key,email_confirmed_at,app_metadata,user_metadata,source_sealed)
                VALUES (?,?,?,UTC_TIMESTAMP(6),'{}','{}',?)`,
            [userId, email, email, sealAccountRecord({ fixture: true }, encryptionKey, `account:${userId}`)]);
            const provider = { siteOrigin: "https://github-qa.invalid", clientId: "Iv1.fixtureclient",
                authorize: (state) => `https://github.com/login/oauth/authorize?state=${state}` };
            const browserSession = await authentication.issueSession(connection, { id: userId, password_hash: null }, now);
            const start = async (token = null) => {
                const result = await beginGitHubAuthorization(authentication, provider, token, "/account/", userId);
                assert.equal(result.state, "redirect");
                checks++;
                return { ...result, stateToken: new URL(result.url).searchParams.get("state") };
            };
            const flow = await start(browserSession.token);
            assert.equal(await consumeGitHubAuthorization(authentication, provider, flow.stateToken, "x".repeat(43), browserSession.token), null);
            const authorization = await consumeGitHubAuthorization(authentication, provider, flow.stateToken, flow.browser, browserSession.token);
            assert.equal(authorization.userId, userId);
            assert.equal(await consumeGitHubAuthorization(authentication, provider, flow.stateToken, flow.browser, browserSession.token), null);
            checks += 3;
            const subject = String(randomInt(100000000000, 281474976710655));
            const identity = { subject, login: "qa-fixture", email };
            assert.equal((await completeGitHubIdentity(authentication, identity, authorization, browserSession.token)).state, "linked");
            assert.equal((await completeGitHubIdentity(authentication, identity, authorization, browserSession.token)).state, "linked");
            const [linked] = await connection.execute("SELECT user_id FROM platform_identities WHERE provider='github' AND provider_id=?", [subject]);
            assert.deepEqual(linked.map((row) => row.user_id), [userId]);
            checks += 3;
            const anonymous = { userId: null, sessionHash: null, expiresAt: now + 600000 };
            const signedIn = await completeGitHubIdentity(authentication, identity, anonymous, null);
            assert.equal(signedIn.state, "authenticated");
            assert.equal((await authentication.session(signedIn.token)).userId, userId);
            assert.equal((await completeGitHubIdentity(authentication, { ...identity, subject: "1" + subject }, anonymous, null)).state,
                "registration_required");
            checks += 3;
            const factorId = randomUUID();
            await connection.execute(`INSERT INTO platform_mfa_factors
                (id,user_id,factor_type,status,friendly_name,source_sealed) VALUES (?,?,'totp','verified','QA GitHub factor',?)`,
            [factorId, userId, sealAccountRecord({ fixture: true }, encryptionKey, `factor:${factorId}`)]);
            const challenged = await completeGitHubIdentity(authentication, identity, anonymous, null);
            assert.equal(challenged.state, "mfa_required");
            const [challenges] = await connection.execute("SELECT user_id,password_digest FROM platform_auth_challenges WHERE token_hash=?",
                [tokenDigest(challenged.challengeToken)]);
            assert.equal(challenges[0].user_id, userId);
            assert.equal(challenges[0].password_digest.length, 64);
            assert.equal((await completeGitHubIdentity(authentication, identity, authorization, browserSession.token)).state, "invalid_session");
            checks += 4;
            const [audits] = await connection.execute(`SELECT action,metadata FROM platform_audit_events
                WHERE actor_user_id=? AND action LIKE 'account.github_%'`, [userId]);
            assert.deepEqual(audits.map((row) => row.action).sort(), ["account.github_identity_linked", "account.github_mfa_requested", "account.github_sign_in"]);
            assert.ok(!JSON.stringify(audits).includes(browserSession.token));
            checks += 2;
            const expired = await start();
            now += 600000;
            assert.equal(await consumeGitHubAuthorization(authentication, provider, expired.stateToken, expired.browser, null), null);
            checks++;
        } finally {
            authentication.close();
        }
        throw rollback;
    }), (error) => error === rollback);
    return checks;
}
