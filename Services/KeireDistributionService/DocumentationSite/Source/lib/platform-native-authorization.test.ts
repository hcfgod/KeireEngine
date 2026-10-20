import { describe, expect, it, vi } from "vitest";
import { createNativeOAuthPolicy } from "./platform-native-oauth-policy.mjs";
import { createNativeAuthorization, decideNativeAuthorization, exchangeNativeAuthorization } from "./platform-native-authorization.mjs";
import { tokenDigest } from "./platform-auth-crypto.mjs";

function fixture() {
    const policy = createNativeOAuthPolicy({ siteOrigin: "https://platform.invalid", clientId: "keire-hub-desktop" });
    const browserToken = "b".repeat(43);
    const clock = { value: 1800000000000 };
    const now = () => clock.value;
    const account = { id: "11111111-1111-4111-8111-111111111111",
        email: "fixture@invalid.example", email_confirmed_at: "2026-01-01", password_hash: "fixture-hash" };
    const session = { userId: account.id, assurance: 1, expiresAt: clock.value + 3600000 };
    const browser = { user_id: account.id, expires_ms: session.expiresAt };
    const state = { request: null, browser, factors: [] };
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn(async (sql, parameters) => {
            if (sql.startsWith("INSERT INTO platform_native_authorizations")) {
                const fields = ["request_hash", "client_id", "redirect_uri", "scope", "state", "nonce", "challenge", "issued_ms", "expires_ms"];
                state.request = { ...Object.fromEntries(fields.map((field, index) => [field, parameters[index]])),
                    decision: "pending", consumed: 0 };
            } else if (sql.startsWith("SELECT * FROM platform_native_authorizations")) {
                const field = sql.includes("code_hash=?") ? "code_hash" : "request_hash";
                return [[state.request].filter((request) => request?.[field] === parameters[0])];
            } else if (sql.startsWith("SELECT * FROM platform_accounts")) {
                return [[account]];
            } else if (sql.startsWith("SELECT * FROM platform_mfa_factors")) {
                return [state.factors];
            } else if (sql.startsWith("SELECT * FROM platform_auth_sessions")) {
                return [state.browser ? [state.browser] : []];
            } else if (sql.includes("SET decision='approved'")) {
                const fields = ["code_hash", "code_expires_ms", "user_id", "browser_session_hash", "account_binding", "assurance"];
                Object.assign(state.request, Object.fromEntries(fields.map((field, index) => [field, parameters[index]])),
                    { decision: "approved" });
            } else if (sql.includes("SET decision='denied'")) {
                state.request.decision = "denied";
            } else if (sql.includes("SET consumed=TRUE")) {
                state.request.consumed = 1;
            }
            return [{ affectedRows: 1 }];
        }),
    };
    const pool = { execute: connection.execute, getConnection: vi.fn(async () => connection) };
    const authentication = { withSessionTransaction: vi.fn(async (_token, action) => action(connection, session)) };
    const issueSession = vi.fn(async () => ({ access_token: "synthetic-native-token" }));
    const parameters = new URLSearchParams({ response_type: "code", client_id: policy.clientId,
        redirect_uri: policy.redirectUri, scope: "openid email profile",
        state: "s".repeat(43), nonce: "n".repeat(43),
        code_challenge: "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", code_challenge_method: "S256" });
    async function approvedCode() {
        const { requestToken } = await createNativeAuthorization(pool, policy, parameters, now);
        const result = await decideNativeAuthorization(authentication, browserToken, policy, requestToken, true, now);
        return { requestToken, code: new URL(result.redirectUrl).searchParams.get("code") };
    }
    const exchange = (code, override = {}) => exchangeNativeAuthorization(pool, policy, {
        clientId: policy.clientId, redirectUri: policy.redirectUri, code,
        verifier: "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", ...override,
    }, issueSession, now);
    return { pool, policy, parameters, now, clock, account, state, connection, authentication,
        browserToken, approvedCode, exchange, issueSession };
}

describe("single-use native authorization", () => {
    it("stores only hashes of request and code tokens and issues a bound session once", async () => {
        const value = fixture();
        const { requestToken, code } = await value.approvedCode();
        expect(value.state.request.request_hash).toBe(tokenDigest(requestToken));
        expect(value.state.request.code_hash).toBe(tokenDigest(code));
        expect(JSON.stringify(value.state.request)).not.toContain(code);
        expect(await value.exchange(code)).toEqual({ access_token: "synthetic-native-token" });
        expect(value.issueSession.mock.calls[0][1].request.nonce).toBe("n".repeat(43));
        await expect(value.exchange(code)).rejects.toMatchObject({ code: "invalid_grant" });
        expect(value.issueSession).toHaveBeenCalledOnce();
    });

    it.each([{ verifier: "x".repeat(43) }, { clientId: "another-client" },
        { redirectUri: "https://other.invalid/callback/" }])("rejects a changed exchange binding", async (override) => {
        const value = fixture();
        const { code } = await value.approvedCode();
        await expect(value.exchange(code, override)).rejects.toMatchObject({ code: "invalid_grant" });
        expect(value.issueSession).not.toHaveBeenCalled();
    });

    it.each(["expired", "logout", "password", "factor", "unconfirmed"])("rejects authorization after %s", async (change) => {
        const value = fixture();
        const { code } = await value.approvedCode();
        if (change === "expired") {
            value.clock.value += 60000;
        }
        if (change === "logout") {
            value.state.browser = null;
        }
        if (change === "password") {
            value.account.password_hash = "changed";
        }
        if (change === "factor") {
            value.state.factors.push({ id: "new", factor_type: "totp", source_sealed: "opaque" });
        }
        if (change === "unconfirmed") {
            value.account.email_confirmed_at = null;
        }
        await expect(value.exchange(code)).rejects.toMatchObject({ code: "invalid_grant" });
        expect(value.issueSession).not.toHaveBeenCalled();
    });

    it("does not consume a code if native session creation fails", async () => {
        const value = fixture();
        const { code } = await value.approvedCode();
        value.issueSession.mockRejectedValue(new Error("Synthetic session storage failure"));
        await expect(value.exchange(code)).rejects.toThrow("Synthetic session storage failure");
        expect(value.state.request.consumed).toBe(0);
        expect(value.connection.rollback).toHaveBeenCalledOnce();
    });

    it("does not approve the same browser request twice", async () => {
        const value = fixture();
        const { requestToken } = await value.approvedCode();
        await expect(decideNativeAuthorization(value.authentication, value.browserToken,
            value.policy, requestToken, true, value.now)).rejects.toMatchObject({ code: "invalid_grant" });
    });

    it("rolls back when session issuance outlasts the authorization code", async () => {
        const value = fixture();
        const { code } = await value.approvedCode();
        value.issueSession.mockImplementation(async () => {
            value.clock.value += 60000;
            return { access_token: "synthetic-native-token" };
        });
        await expect(value.exchange(code)).rejects.toMatchObject({ code: "invalid_grant" });
        expect(value.state.request.consumed).toBe(0);
        expect(value.connection.rollback).toHaveBeenCalledOnce();
    });

    it("returns a denial only to the configured callback and preserves state", async () => {
        const value = fixture();
        const { requestToken } = await createNativeAuthorization(value.pool, value.policy, value.parameters, value.now);
        const result = await decideNativeAuthorization(value.authentication, value.browserToken,
            value.policy, requestToken, false, value.now);
        const redirect = new URL(result.redirectUrl);
        expect(redirect.origin + redirect.pathname).toBe(value.policy.redirectUri);
        expect(redirect.searchParams.get("error")).toBe("access_denied");
        expect(redirect.searchParams.get("state")).toBe("s".repeat(43));
        expect(redirect.searchParams.has("code")).toBe(false);
    });
});
