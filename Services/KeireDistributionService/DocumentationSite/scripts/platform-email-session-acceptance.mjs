import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { newOpaqueToken, tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";
import { sealAccountRecord } from "../Source/lib/platform-account-import.mjs";

export async function verifyEmailSessionAssurance(pool, auth, userId, key, now) {
    const factorId = randomUUID();
    const token = newOpaqueToken();
    const [accounts] = await pool.execute("SELECT email,password_hash FROM platform_accounts WHERE id = ?", [userId]);
    const account = accounts[0];
    try {
        await pool.execute(`INSERT INTO platform_mfa_factors
            (id,user_id,factor_type,status,friendly_name,source_sealed) VALUES (?,?,'email','verified','Email',?)`,
        [factorId, userId, sealAccountRecord({ email: account.email }, key, `mfa:${factorId}`)]);
        await pool.execute(`INSERT INTO platform_auth_sessions
            (token_hash,user_id,password_digest,assurance_level,factor_id,expires_ms) VALUES (?,?,?,1,?,?)`,
        [tokenDigest(token), userId, createHash("sha256").update(account.password_hash).digest("hex"), factorId, now + 60000]);
        assert.equal((await auth.session(token)).method, "email");
        assert.equal((await auth.session(token)).assurance, 1);
        await pool.execute("UPDATE platform_auth_sessions SET assurance_level = 2 WHERE token_hash = ?", [tokenDigest(token)]);
        assert.equal(await auth.session(token), null);
        await pool.execute("UPDATE platform_auth_sessions SET assurance_level = 1 WHERE token_hash = ?", [tokenDigest(token)]);
        await pool.execute("UPDATE platform_mfa_factors SET source_sealed = ? WHERE id = ?",
            [sealAccountRecord({ email: "changed@example.com" }, key, `mfa:${factorId}`), factorId]);
        assert.equal(await auth.session(token), null);
        await pool.execute("UPDATE platform_mfa_factors SET source_sealed = ?,status = 'unverified' WHERE id = ?",
            [sealAccountRecord({ email: account.email }, key, `mfa:${factorId}`), factorId]);
        assert.equal(await auth.session(token), null);
        return 5;
    } finally {
        await pool.execute("DELETE FROM platform_auth_sessions WHERE token_hash = ?", [tokenDigest(token)]);
        await pool.execute("DELETE FROM platform_mfa_factors WHERE id = ? AND user_id = ?", [factorId, userId]);
    }
}
