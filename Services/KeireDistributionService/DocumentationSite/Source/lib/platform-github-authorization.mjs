import { createHash, randomUUID } from "node:crypto";
import { newOpaqueToken, tokenDigest } from "./platform-auth-crypto.mjs";
import { sealAccountRecord, openAccountRecord } from "./platform-account-import.mjs";
import { accountAvailable } from "./platform-local-auth.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

const lifetime = 10 * 60 * 1000;
const digest = (value) => createHash("sha256").update(value).digest("hex");

export const githubAuthorizationSchema = `CREATE TABLE platform_github_authorizations (
    state_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    browser_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    payload_sealed BLOB NOT NULL,
    expires_ms BIGINT UNSIGNED NOT NULL,
    consumed BOOLEAN NOT NULL DEFAULT FALSE,
    INDEX github_authorization_expiry(expires_ms)
) ENGINE=InnoDB`;

export function githubNextPath(value) {
    if (typeof value !== "string" || value.length > 2048 || !value.startsWith("/") || value.startsWith("//") ||
        /[\\\u0000-\u0020\u007f]/.test(value)) {
        return "/account/";
    }
    const url = new URL(value, "https://keire.invalid");
    return url.origin === "https://keire.invalid" ? `${url.pathname}${url.search}${url.hash}` : "/account/";
}

export async function beginGitHubAuthorization(authentication, provider, sessionToken, next, clientIdentity) {
    const now = authentication.now();
    if (typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 200) {
        return { state: "invalid_request" };
    }
    if (!await authentication.reserveAttempt(`github:${clientIdentity}`, `github:${clientIdentity}`, now)) {
        return { state: "rate_limited" };
    }
    const session = sessionToken ? await authentication.session(sessionToken) : null;
    if (sessionToken && !session) {
        return { state: "invalid_session" };
    }
    const state = newOpaqueToken();
    const browser = newOpaqueToken();
    const verifier = newOpaqueToken();
    const payload = sealAccountRecord({ verifier, expiresAt: now + lifetime, next: githubNextPath(next), origin: provider.siteOrigin,
        clientId: provider.clientId, userId: session?.userId ?? null, sessionHash: session ? tokenDigest(sessionToken) : null },
    authentication.encryptionKey, `github:${tokenDigest(state)}`);
    try {
        await authentication.pool.execute(`INSERT INTO platform_github_authorizations
            (state_hash,browser_hash,payload_sealed,expires_ms) VALUES (?,?,?,?)`,
        [tokenDigest(state), tokenDigest(browser), payload, now + lifetime]);
    } finally {
        payload.fill(0);
    }
    return { state: "redirect", browser, url: provider.authorize(state, verifier) };
}

/** Consume once before contacting GitHub. A failed exchange requires a fresh flow, never token replay. */
export async function consumeGitHubAuthorization(authentication, provider, state, browser, sessionToken) {
    const stateHash = tokenDigest(state);
    const browserHash = tokenDigest(browser);
    if (!stateHash || !browserHash) {
        return null;
    }
    return platformTransaction(authentication.pool, async (connection) => {
        const [rows] = await connection.execute(
            "SELECT * FROM platform_github_authorizations WHERE state_hash=? FOR UPDATE", [stateHash]);
        const record = rows[0];
        if (!record || record.consumed || record.browser_hash !== browserHash || Number(record.expires_ms) <= authentication.now()) {
            return null;
        }
        const payload = openAccountRecord(record.payload_sealed, authentication.encryptionKey, `github:${stateHash}`);
        if (payload.origin !== provider.siteOrigin || payload.clientId !== provider.clientId ||
            payload.sessionHash !== (sessionToken ? tokenDigest(sessionToken) : null)) {
            return null;
        }
        await connection.execute("UPDATE platform_github_authorizations SET consumed=TRUE WHERE state_hash=?", [stateHash]);
        return payload;
    });
}

async function auditGitHub(connection, userId, action) {
    await connection.execute(`INSERT INTO platform_audit_events
        (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
        VALUES (UTC_TIMESTAMP(6),?,NULL,?,'account',?,?,?)`,
    [userId, action, userId, randomUUID(), JSON.stringify({ provider: "github" })]);
}

/** Provider subjects are authoritative. Matching an email never links or signs into another account. */
export async function completeGitHubIdentity(authentication, identity, authorization, sessionToken) {
    if (!Number.isSafeInteger(authorization.expiresAt) || authorization.expiresAt <= authentication.now()) {
        return { state: "invalid_identity" };
    }
    if (typeof identity?.subject !== "string" || !/^[1-9][0-9]{0,15}$/.test(identity.subject) ||
        typeof identity.email !== "string" || identity.email.length > 320) {
        return { state: "invalid_identity" };
    }
    if (authorization.userId) {
        if (!sessionToken || tokenDigest(sessionToken) !== authorization.sessionHash) {
            return { state: "invalid_session" };
        }
        return await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
            if (session.userId !== authorization.userId || authorization.expiresAt <= authentication.now()) {
                return { state: "invalid_session" };
            }
            const [factors] = await connection.execute(`SELECT id,factor_type FROM platform_mfa_factors
                WHERE user_id=? AND status='verified'`, [session.userId]);
            if (factors.some((factor) => factor.factor_type !== "email") && session.assurance !== 2) {
                return { state: "verification_required" };
            }
            const [identities] = await connection.execute(`SELECT user_id,provider_id FROM platform_identities
                WHERE provider='github' AND (provider_id=? OR user_id=?) FOR UPDATE`, [identity.subject, session.userId]);
            if (identities.some((row) => row.user_id !== session.userId || row.provider_id !== identity.subject)) {
                return { state: "identity_conflict" };
            }
            if (!identities.length) {
                const data = JSON.stringify({ sub: identity.subject, user_name: identity.login, email: identity.email,
                    email_verified: true });
                try {
                    await connection.execute(`INSERT INTO platform_identities
                        (id,user_id,provider,provider_id,identity_data,created_at,updated_at,source_record)
                        VALUES (?,?,'github',?,?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),?)`,
                    [randomUUID(), session.userId, identity.subject, data, JSON.stringify({ source: "local-github" })]);
                } catch (error) {
                    if (error?.code === "ER_DUP_ENTRY") {
                        return { state: "identity_conflict" };
                    }
                    throw error;
                }
                await auditGitHub(connection, session.userId, "account.github_identity_linked");
            }
            return { state: "linked", next: authorization.next };
        }) ?? { state: "invalid_session" };
    }
    return platformTransaction(authentication.pool, async (connection) => {
        const [identities] = await connection.execute(
            "SELECT user_id FROM platform_identities WHERE provider='github' AND provider_id=?", [identity.subject]);
        if (identities.length !== 1) {
            return { state: "registration_required" };
        }
        const [accounts] = await connection.execute("SELECT * FROM platform_accounts WHERE id=? FOR UPDATE", [identities[0].user_id]);
        const account = accounts[0];
        const now = authentication.now();
        if (!accountAvailable(account, now) || authorization.expiresAt <= now) {
            return { state: "invalid_identity" };
        }
        // Recheck under the account lock: identity removal or transfer cannot race authentication.
        const [current] = await connection.execute(`SELECT user_id FROM platform_identities
            WHERE provider='github' AND provider_id=? FOR UPDATE`, [identity.subject]);
        if (current.length !== 1 || current[0].user_id !== account.id) {
            return { state: "invalid_identity" };
        }
        const [factors] = await connection.execute(`SELECT id,factor_type,friendly_name FROM platform_mfa_factors
            WHERE user_id=? AND status='verified'`, [account.id]);
        if (!factors.length) {
            await auditGitHub(connection, account.id, "account.github_sign_in");
            return authentication.issueSession(connection, account, now);
        }
        if (factors.some((factor) => !["totp", "email"].includes(factor.factor_type))) {
            return { state: "unsupported_mfa" };
        }
        const challengeToken = newOpaqueToken();
        await connection.execute(`INSERT INTO platform_auth_challenges
            (token_hash,user_id,password_digest,expires_ms) VALUES (?,?,?,?)`,
        [tokenDigest(challengeToken), account.id, digest(account.password_hash ?? ""), now + 300000]);
        await auditGitHub(connection, account.id, "account.github_mfa_requested");
        return { state: "mfa_required", challengeToken, factors };
    });
}
