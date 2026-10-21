import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { newOpaqueToken, tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";
import { createNativeAuthorization, exchangeNativeAuthorization, nativeAccountBinding } from "../Source/lib/platform-native-authorization.mjs";
import { createNativeOAuthPolicy } from "../Source/lib/platform-native-oauth-policy.mjs";
import { issueNativeSession } from "../Source/lib/platform-native-sessions.mjs";

/** Seed approved synthetic consent, then race real redemption on two physical connections. */
export async function redeemNativeCodeConcurrently(pool, signer, account, factors) {
    const policy = createNativeOAuthPolicy({ siteOrigin: "https://concurrency.invalid", clientId: signer.clientId });
    const browserHash = tokenDigest(newOpaqueToken());
    const code = newOpaqueToken();
    const now = Date.now();
    const verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    let requestHash;
    try {
        await pool.execute(`INSERT INTO platform_auth_sessions
            (token_hash,user_id,password_digest,assurance_level,expires_ms) VALUES (?,?,?,?,?)`,
        [browserHash, account.id, createHash("sha256").update(account.password_hash).digest("hex"), 1, now + 600000]);
        const created = await createNativeAuthorization(pool, policy, new URLSearchParams({
            response_type: "code", client_id: policy.clientId, redirect_uri: policy.redirectUri,
            scope: "openid email profile", state: "s".repeat(43), nonce: "n".repeat(43),
            code_challenge: createHash("sha256").update(verifier).digest("base64url"), code_challenge_method: "S256",
        }));
        requestHash = tokenDigest(created.requestToken);
        await pool.execute(`UPDATE platform_native_authorizations SET decision='approved',code_hash=?,code_expires_ms=?,
            user_id=?,browser_session_hash=?,account_binding=?,assurance=1 WHERE request_hash=?`,
        [tokenDigest(code), now + 60000, account.id, browserHash, nativeAccountBinding(account, factors), requestHash]);
        const connections = [await pool.getConnection()];
        try {
            connections.push(await pool.getConnection());
            assert.notEqual(connections[0].threadId, connections[1].threadId);
        } catch (error) {
            for (const connection of connections) {
                connection.release();
            }
            throw error;
        }
        const outcomes = await Promise.allSettled(connections.map((connection) => exchangeNativeAuthorization(
            { getConnection: async () => connection }, policy,
            { code, clientId: policy.clientId, redirectUri: policy.redirectUri, verifier },
            (transaction, identity) => issueNativeSession(transaction, signer, identity))));
        const successes = outcomes.filter((result) => result.status === "fulfilled");
        const failures = outcomes.filter((result) => result.status === "rejected");
        assert.equal(successes.length, 1);
        assert.equal(failures.length, 1);
        assert.equal(failures[0].reason.code, "invalid_grant");
        const [[count]] = await pool.execute("SELECT COUNT(*) AS total FROM platform_native_sessions WHERE user_id=?", [account.id]);
        assert.equal(Number(count.total), 1);
        return successes[0].value;
    } finally {
        if (requestHash) {
            await pool.execute("DELETE FROM platform_native_authorizations WHERE request_hash=?", [requestHash]);
        }
        await pool.execute("DELETE FROM platform_auth_sessions WHERE token_hash=? AND user_id=?", [browserHash, account.id]);
    }
}
