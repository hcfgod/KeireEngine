import { randomUUID } from "node:crypto";
import { newOpaqueToken, tokenDigest } from "./platform-auth-crypto.mjs";
import { nativeAccountBinding } from "./platform-native-authorization.mjs";
import { accountAvailable } from "./platform-local-auth.mjs";
import { platformTransaction } from "./platform-mysql.mjs";
import { NativeOAuthError } from "./platform-native-oauth-policy.mjs";

const sessionLifetimeMs = 30 * 24 * 60 * 60 * 1000;
const accessLifetimeMs = 15 * 60 * 1000;

function invalidGrant() {
    return new NativeOAuthError("invalid_grant", "Sign in again to restore desktop access.");
}

function checkedTime(clock) {
    const now = clock();
    if (!Number.isSafeInteger(now) || now < 0 || !Number.isSafeInteger(now + sessionLifetimeMs)) {
        throw new Error("Invalid native session clock.");
    }
    return now;
}

async function audit(connection, session, action, metadata = {}) {
    await connection.execute(`INSERT INTO platform_audit_events
        (occurred_at,actor_user_id,action,target_type,target_id,correlation_id,metadata)
        VALUES (UTC_TIMESTAMP(6),?,?,'platform_native_session',?,?,?)`,
    [session.user_id, action, session.id, randomUUID(), JSON.stringify(metadata)]);
}

async function tokens(connection, signer, session, account, now) {
    const accessToken = newOpaqueToken();
    const refreshToken = newOpaqueToken();
    const expiresAt = Math.min(now + accessLifetimeMs, Number(session.expires_ms));
    if (expiresAt <= now) {
        throw invalidGrant();
    }
    const idToken = await signer.sign({
        userId: session.user_id, sessionId: session.id, nonce: session.nonce,
        assurance: Number(session.assurance), authenticatedAt: Number(session.authenticated_ms),
        scope: session.scope, email: account.email, emailVerified: Boolean(account.email_confirmed_at),
        accessToken, now,
    });
    await connection.execute(`INSERT INTO platform_native_access_tokens
        (token_hash,session_id,issued_ms,expires_ms) VALUES (?,?,?,?)`,
    [tokenDigest(accessToken), session.id, now, expiresAt]);
    await connection.execute(`INSERT INTO platform_native_refresh_tokens
        (token_hash,session_id,issued_ms,expires_ms) VALUES (?,?,?,?)`,
    [tokenDigest(refreshToken), session.id, now, session.expires_ms]);
    return {
        access_token: accessToken, refresh_token: refreshToken, id_token: idToken,
        token_type: "Bearer", expires_in: Math.floor((expiresAt - now) / 1000), scope: session.scope,
    };
}

/** Call only from the locked authorization-code exchange transaction. */
export async function issueNativeSession(connection, signer, { account, request, now }) {
    checkedTime(() => now);
    if (!accountAvailable(account, now) || signer.clientId !== request.client_id) {
        throw invalidGrant();
    }
    const session = {
        id: randomUUID(), user_id: account.id, client_id: request.client_id,
        scope: request.scope, nonce: request.nonce, account_binding: request.account_binding,
        assurance: Number(request.assurance), authenticated_ms: now, expires_ms: now + sessionLifetimeMs,
    };
    await connection.execute(`INSERT INTO platform_native_sessions
        (id,user_id,client_id,scope,nonce,account_binding,assurance,authenticated_ms,expires_ms)
        VALUES (?,?,?,?,?,?,?,?,?)`,
    [session.id, session.user_id, session.client_id, session.scope, session.nonce,
        session.account_binding, session.assurance, now, session.expires_ms]);
    const result = await tokens(connection, signer, session, account, now);
    await audit(connection, session, "account.native_session.created", { clientId: session.client_id });
    return result;
}

/** Reuse revokes the whole session. Return the rejection after commit so revocation cannot roll back. */
export async function refreshNativeSession(pool, signer, clientId, refreshToken, clock = Date.now) {
    const hash = tokenDigest(refreshToken);
    if (!hash || clientId !== signer.clientId) {
        throw invalidGrant();
    }
    const result = await platformTransaction(pool, async (connection) => {
        const [[refresh]] = await connection.execute(
            "SELECT * FROM platform_native_refresh_tokens WHERE token_hash=? FOR UPDATE", [hash]);
        if (!refresh) {
            return null;
        }
        const [[session]] = await connection.execute(
            "SELECT * FROM platform_native_sessions WHERE id=? FOR UPDATE", [refresh.session_id]);
        let now = checkedTime(clock);
        if (!session || session.client_id !== clientId || session.revoked_ms !== null
            || Number(session.expires_ms) <= now || Number(refresh.expires_ms) <= now) {
            return null;
        }
        if (refresh.used_ms !== null) {
            await connection.execute("UPDATE platform_native_sessions SET revoked_ms=? WHERE id=?", [now, session.id]);
            await audit(connection, session, "account.native_session.refresh_reuse");
            return null;
        }
        const [[account]] = await connection.execute("SELECT * FROM platform_accounts WHERE id=? FOR UPDATE", [session.user_id]);
        const [factors] = await connection.execute(
            "SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified' FOR UPDATE", [session.user_id]);
        now = checkedTime(clock);
        if (!accountAvailable(account, now) || nativeAccountBinding(account, factors) !== session.account_binding) {
            await connection.execute("UPDATE platform_native_sessions SET revoked_ms=? WHERE id=?", [now, session.id]);
            await audit(connection, session, "account.native_session.account_changed");
            return null;
        }
        if (Number(session.expires_ms) <= now || Number(refresh.expires_ms) <= now) {
            return null;
        }
        const replacement = await tokens(connection, signer, session, account, now);
        if (Number(session.expires_ms) <= checkedTime(clock)) {
            throw invalidGrant();
        }
        await connection.execute("UPDATE platform_native_refresh_tokens SET used_ms=? WHERE token_hash=?", [now, hash]);
        await audit(connection, session, "account.native_session.refreshed");
        return replacement;
    });
    if (!result) {
        throw invalidGrant();
    }
    return result;
}

/** Every native API call rechecks the independently revocable session and current account security state. */
export async function withNativeSessionTransaction(pool, accessToken, action, clock = Date.now) {
    const hash = tokenDigest(accessToken);
    if (!hash) {
        return null;
    }
    return platformTransaction(pool, async (connection) => {
        const [[access]] = await connection.execute(
            "SELECT * FROM platform_native_access_tokens WHERE token_hash=? FOR UPDATE", [hash]);
        if (!access || Number(access.expires_ms) <= checkedTime(clock)) {
            return null;
        }
        const [[session]] = await connection.execute(
            "SELECT * FROM platform_native_sessions WHERE id=? FOR UPDATE", [access.session_id]);
        if (!session || session.revoked_ms !== null || Number(session.expires_ms) <= checkedTime(clock)) {
            return null;
        }
        const [[account]] = await connection.execute("SELECT * FROM platform_accounts WHERE id=? FOR UPDATE", [session.user_id]);
        const [factors] = await connection.execute(
            "SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified' FOR UPDATE", [session.user_id]);
        const now = checkedTime(clock);
        if (!accountAvailable(account, now) || nativeAccountBinding(account, factors) !== session.account_binding
            || Number(access.expires_ms) <= now || Number(session.expires_ms) <= now) {
            return null;
        }
        return action(connection, {
            userId: session.user_id, sessionId: session.id, clientId: session.client_id,
            assurance: Number(session.assurance), scope: session.scope, account,
        });
    });
}
