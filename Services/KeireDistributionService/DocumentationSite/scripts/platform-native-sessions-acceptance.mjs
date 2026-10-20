import assert from "node:assert/strict";
import { generateKeyPairSync } from "node:crypto";
import { jwtVerify, createLocalJWKSet } from "jose";
import { nativeSessionSchema } from "../Source/lib/platform-native-session-schema.mjs";
import { nativeAccountBinding } from "../Source/lib/platform-native-authorization.mjs";
import { createNativeIdentitySigner } from "../Source/lib/platform-native-identity.mjs";
import { issueNativeSession, refreshNativeSession, withNativeSessionTransaction } from "../Source/lib/platform-native-sessions.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";

export async function verifyNativeSessions(pool, userId) {
    const raw = await pool.getConnection();
    const signer = createNativeIdentitySigner({
        siteOrigin: "https://qa.invalid", clientId: "keire-hub-desktop",
        privateKey: generateKeyPairSync("ed25519").privateKey,
    });
    const tables = ["platform_native_sessions", "platform_native_access_tokens", "platform_native_refresh_tokens",
        "platform_accounts", "platform_mfa_factors", "platform_audit_events"];
    const names = new RegExp(`\\b(${tables.join("|")})\\b`, "g");
    const connection = {
        execute: (sql, values) => raw.execute(sql.replace(names, (table) => `${table}_qa_native`), values),
        beginTransaction: () => raw.beginTransaction(), commit: () => raw.commit(), rollback: () => raw.rollback(),
        release() {}, destroy: () => raw.destroy(),
    };
    const isolatedPool = { getConnection: async () => connection };
    try {
        for (const schema of nativeSessionSchema) {
            await connection.execute(schema.replace("CREATE TABLE", "CREATE TEMPORARY TABLE"));
        }
        for (const table of ["platform_accounts", "platform_mfa_factors", "platform_audit_events"]) {
            await raw.execute(`CREATE TEMPORARY TABLE ${table}_qa_native LIKE ${table}`);
        }
        // Copy only the harness-owned synthetic identity, never customer accounts.
        await raw.execute("INSERT INTO platform_accounts_qa_native SELECT * FROM platform_accounts WHERE id=?", [userId]);
        await raw.execute("INSERT INTO platform_mfa_factors_qa_native SELECT * FROM platform_mfa_factors WHERE user_id=?", [userId]);
        const [[account]] = await connection.execute("SELECT * FROM platform_accounts WHERE id=?", [userId]);
        const [factors] = await connection.execute("SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified'", [userId]);
        let now = Date.now();
        const request = {
            client_id: signer.clientId, scope: "openid email profile", nonce: "n".repeat(43),
            account_binding: nativeAccountBinding(account, factors), assurance: 2,
        };
        const initial = await platformTransaction(isolatedPool,
            (transaction) => issueNativeSession(transaction, signer, { account, request, now }));
        assert.equal(initial.token_type, "Bearer");
        assert.equal(initial.expires_in, 900);
        assert.notEqual(initial.access_token, initial.refresh_token);
        const { payload } = await jwtVerify(initial.id_token, createLocalJWKSet(signer.jwks), {
            issuer: signer.issuer, audience: signer.clientId, algorithms: ["EdDSA"], currentDate: new Date(now),
        });
        assert.equal(payload.sub, userId);
        const readIdentity = (accessToken) =>
            withNativeSessionTransaction(isolatedPool, accessToken, (_transaction, identity) => identity.userId, () => now);
        assert.equal(await readIdentity(initial.access_token), userId);
        const [[stored]] = await connection.execute("SELECT token_hash FROM platform_native_refresh_tokens");
        assert.equal(stored.token_hash, tokenDigest(initial.refresh_token));
        now += 1000;
        const failAudit = {
            getConnection: async () => ({
                ...connection,
                execute(sql, values) {
                    if (sql.includes("INSERT INTO platform_audit_events")) {
                        throw new Error("Synthetic native audit failure");
                    }
                    return connection.execute(sql, values);
                },
            }),
        };
        await assert.rejects(refreshNativeSession(failAudit, signer, signer.clientId, initial.refresh_token, () => now),
            /Synthetic native audit failure/);
        const [[unused]] = await connection.execute("SELECT used_ms FROM platform_native_refresh_tokens");
        assert.equal(unused.used_ms, null);
        const [[before]] = await connection.execute("SELECT COUNT(*) AS total FROM platform_native_access_tokens");
        assert.equal(Number(before.total), 1);
        const replacement = await refreshNativeSession(isolatedPool, signer, signer.clientId, initial.refresh_token, () => now);
        assert.notEqual(replacement.refresh_token, initial.refresh_token);
        assert.notEqual(replacement.access_token, initial.access_token);
        assert.equal(await readIdentity(replacement.access_token), userId);
        await assert.rejects(refreshNativeSession(isolatedPool, signer, signer.clientId, initial.refresh_token, () => now),
            { code: "invalid_grant" });
        const [[revoked]] = await connection.execute("SELECT revoked_ms FROM platform_native_sessions");
        assert.equal(Number(revoked.revoked_ms), now);
        assert.equal(await readIdentity(initial.access_token), null);
        assert.equal(await readIdentity(replacement.access_token), null);
        await assert.rejects(refreshNativeSession(isolatedPool, signer, signer.clientId, replacement.refresh_token, () => now),
            { code: "invalid_grant" });
        const [[audit]] = await connection.execute(`SELECT COUNT(*) AS total FROM platform_audit_events
            WHERE action='account.native_session.refresh_reuse'`);
        assert.equal(Number(audit.total), 1);
        const otherSession = await platformTransaction(isolatedPool,
            (transaction) => issueNativeSession(transaction, signer, { account, request, now }));
        await connection.execute("UPDATE platform_accounts SET password_hash='changed-fixture-password' WHERE id=?", [userId]);
        assert.equal(await readIdentity(otherSession.access_token), null);
        await assert.rejects(refreshNativeSession(isolatedPool, signer, signer.clientId, otherSession.refresh_token, () => now),
            { code: "invalid_grant" });
        return 21;
    } finally {
        signer.close();
        raw.destroy();
    }
}
