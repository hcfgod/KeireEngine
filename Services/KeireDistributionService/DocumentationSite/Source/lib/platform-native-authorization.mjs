import { createHash, randomUUID } from "node:crypto";
import { newOpaqueToken, tokenDigest } from "./platform-auth-crypto.mjs";
import { accountAvailable } from "./platform-local-auth.mjs";
import { platformTransaction } from "./platform-mysql.mjs";
import { NativeOAuthError, verifyNativePkce } from "./platform-native-oauth-policy.mjs";

export const nativeAuthorizationSchema = `CREATE TABLE platform_native_authorizations (
    request_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    client_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    redirect_uri VARCHAR(2048) NOT NULL,
    scope VARCHAR(128) CHARACTER SET ascii NOT NULL,
    state VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    nonce VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    challenge CHAR(43) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    issued_ms BIGINT UNSIGNED NOT NULL,
    expires_ms BIGINT UNSIGNED NOT NULL,
    decision VARCHAR(16) CHARACTER SET ascii NOT NULL DEFAULT 'pending',
    code_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL UNIQUE,
    code_expires_ms BIGINT UNSIGNED NULL,
    user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
    browser_session_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    account_binding CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
    assurance TINYINT UNSIGNED NULL,
    consumed BOOLEAN NOT NULL DEFAULT FALSE,
    CHECK (expires_ms=issued_ms+600000),
    CHECK (decision IN ('pending','approved','denied')),
    CHECK (assurance IS NULL OR assurance IN (1,2)),
    INDEX native_authorization_expiry(expires_ms)
) ENGINE=InnoDB`;

function invalidGrant() {
    return new NativeOAuthError("invalid_grant", "The desktop authorization expired or is no longer valid.");
}

function accountBinding(account, factors) {
    const enrolled = factors.map((factor) => [factor.id, factor.factor_type, factor.source_sealed])
        .sort((left, right) => left[0].localeCompare(right[0]));
    return createHash("sha256").update(JSON.stringify([
        "Keire.NativeAuthorization.v1", account.id, account.email, account.password_hash, enrolled,
    ])).digest("hex");
}

function timestamp(now) {
    const value = now();
    if (!Number.isSafeInteger(value) || value < 0 || !Number.isSafeInteger(value + 600000)) {
        throw new Error("Invalid authorization clock.");
    }
    return value;
}

/** HTTP caller must enforce its anonymous request rate limit before creating a request. */
export async function createNativeAuthorization(pool, policy, parameters, now = Date.now) {
    const request = policy.authorize(parameters);
    const issued = timestamp(now);
    const requestToken = newOpaqueToken();
    await pool.execute(`INSERT INTO platform_native_authorizations
        (request_hash,client_id,redirect_uri,scope,state,nonce,challenge,issued_ms,expires_ms)
        VALUES (?,?,?,?,?,?,?,?,?)`,
    [tokenDigest(requestToken), request.clientId, request.redirectUri, request.scope,
        request.state, request.nonce, request.challenge, issued, issued + 600000]);
    return { requestToken, expiresAt: issued + 600000 };
}

/** Requires the normal browser session/MFA check and an exact-origin CSRF-checked decision POST. */
export async function decideNativeAuthorization(authentication, browserToken, policy, requestToken, approve, now = Date.now) {
    const requestHash = tokenDigest(requestToken);
    if (!requestHash || typeof approve !== "boolean") {
        throw invalidGrant();
    }
    const result = await authentication.withSessionTransaction(browserToken, async (connection, session) => {
        const [[request]] = await connection.execute(
            "SELECT * FROM platform_native_authorizations WHERE request_hash=? FOR UPDATE", [requestHash]);
        const current = timestamp(now);
        if (!request || request.decision !== "pending" || Number(request.expires_ms) <= current
            || session.expiresAt <= current
            || request.client_id !== policy.clientId || request.redirect_uri !== policy.redirectUri) {
            throw invalidGrant();
        }
        const redirect = new URL(policy.redirectUri);
        redirect.searchParams.set("state", request.state);
        if (!approve) {
            await connection.execute("UPDATE platform_native_authorizations SET decision='denied' WHERE request_hash=?", [requestHash]);
            redirect.searchParams.set("error", "access_denied");
            return { redirectUrl: redirect.href };
        }
        const [[account]] = await connection.execute("SELECT * FROM platform_accounts WHERE id=? FOR UPDATE", [session.userId]);
        const [factors] = await connection.execute(
            "SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified' FOR UPDATE", [session.userId]);
        const approvedAt = timestamp(now);
        if (!accountAvailable(account, approvedAt) || ![1, 2].includes(session.assurance)
            || session.expiresAt <= approvedAt || Number(request.expires_ms) <= approvedAt) {
            throw invalidGrant();
        }
        const code = newOpaqueToken();
        const codeExpiry = Math.min(approvedAt + 60000, Number(request.expires_ms), session.expiresAt);
        await connection.execute(`UPDATE platform_native_authorizations SET decision='approved',
            code_hash=?,code_expires_ms=?,user_id=?,browser_session_hash=?,account_binding=?,assurance=?
            WHERE request_hash=?`,
        [tokenDigest(code), codeExpiry, session.userId, tokenDigest(browserToken),
            accountBinding(account, factors), session.assurance, requestHash]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,'account.native_authorization.approved','platform_account',?,?,?)`,
        [session.userId, session.userId, randomUUID(), JSON.stringify({ clientId: policy.clientId })]);
        redirect.searchParams.set("code", code);
        return { redirectUrl: redirect.href };
    });
    if (!result) {
        throw new NativeOAuthError("login_required", "Sign in before authorizing the desktop app.");
    }
    return result;
}

/** issueSession must persist the independent native session using this transaction's connection. */
export async function exchangeNativeAuthorization(pool, policy, input, issueSession, now = Date.now) {
    const codeHash = tokenDigest(input?.code);
    if (!codeHash || input.clientId !== policy.clientId || input.redirectUri !== policy.redirectUri
        || typeof issueSession !== "function") {
        throw invalidGrant();
    }
    return platformTransaction(pool, async (connection) => {
        const [[request]] = await connection.execute(
            "SELECT * FROM platform_native_authorizations WHERE code_hash=? FOR UPDATE", [codeHash]);
        if (!request || request.decision !== "approved" || Number(request.consumed) !== 0
            || request.client_id !== policy.clientId || request.redirect_uri !== policy.redirectUri
            || Number(request.code_expires_ms) <= timestamp(now)
            || !verifyNativePkce(request.challenge, input.verifier)) {
            throw invalidGrant();
        }
        const [[account]] = await connection.execute("SELECT * FROM platform_accounts WHERE id=? FOR UPDATE", [request.user_id]);
        const [factors] = await connection.execute(
            "SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified' FOR UPDATE", [request.user_id]);
        const [[browser]] = await connection.execute(
            "SELECT * FROM platform_auth_sessions WHERE token_hash=? FOR UPDATE", [request.browser_session_hash]);
        const current = timestamp(now);
        if (!accountAvailable(account, current) || accountBinding(account, factors) !== request.account_binding
            || !browser || browser.user_id !== request.user_id || Number(browser.expires_ms) <= current
            || Number(request.code_expires_ms) <= current) {
            throw invalidGrant();
        }
        const result = await issueSession(connection, { account, request, now: current });
        if (Number(request.code_expires_ms) <= timestamp(now)) {
            throw invalidGrant();
        }
        await connection.execute("UPDATE platform_native_authorizations SET consumed=TRUE WHERE request_hash=?",
            [request.request_hash]);
        return result;
    });
}
