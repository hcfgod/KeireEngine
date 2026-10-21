import assert from "node:assert/strict";
import { generateKeyPairSync } from "node:crypto";
import { createNativeIdentitySigner } from "../Source/lib/platform-native-identity.mjs";
import { refreshNativeSession, withNativeSessionTransaction } from "../Source/lib/platform-native-sessions.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { redeemNativeCodeConcurrently } from "./platform-native-code-concurrency.mjs";

/** Shared native tables, but only the harness-created synthetic identity; never customer sessions. */
export async function verifyNativeRefreshConcurrency(pool, userId) {
    const [[account]] = await pool.execute("SELECT * FROM platform_accounts WHERE id=?", [userId]);
    assert.equal(account?.email, `qa-${userId}@invalid.example`);
    const [[existing]] = await pool.execute("SELECT COUNT(*) AS count FROM platform_native_sessions WHERE user_id=?", [userId]);
    assert.equal(Number(existing.count), 0);
    const [factors] = await pool.execute("SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified'", [userId]);
    const signer = createNativeIdentitySigner({ siteOrigin: "https://concurrency.invalid", clientId: "keire-hub-desktop",
        privateKey: generateKeyPairSync("ed25519").privateKey });
    try {
        const tokens = await redeemNativeCodeConcurrently(pool, signer, account, factors);
        // Hold both physical connections before starting either transaction, proving cross-connection contention.
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
        const outcomes = await Promise.allSettled(connections.map((connection) => refreshNativeSession(
            { getConnection: async () => connection }, signer, signer.clientId, tokens.refresh_token)));
        const successes = outcomes.filter((result) => result.status === "fulfilled");
        const failures = outcomes.filter((result) => result.status === "rejected");
        assert.equal(successes.length, 1);
        assert.equal(failures.length, 1);
        assert.equal(failures[0].reason.code, "invalid_grant");
        const replacement = successes[0].value;
        assert.equal(await withNativeSessionTransaction(pool, replacement.access_token, () => true), null);
        await assert.rejects(refreshNativeSession(pool, signer, signer.clientId, replacement.refresh_token),
            { code: "invalid_grant" });
        const [[session]] = await pool.execute("SELECT revoked_ms FROM platform_native_sessions WHERE user_id=?", [userId]);
        assert.notEqual(session.revoked_ms, null);
        return 12;
    } finally {
        signer.close();
        await platformTransaction(pool, async (connection) => {
            for (const table of ["platform_native_access_tokens", "platform_native_refresh_tokens"]) {
                await connection.execute(`DELETE token FROM ${table} token
                    INNER JOIN platform_native_sessions session ON session.id=token.session_id WHERE session.user_id=?`, [userId]);
            }
            await connection.execute("DELETE FROM platform_native_sessions WHERE user_id=?", [userId]);
        });
    }
}
