import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { LocalPasswordAuthentication } from "../Source/lib/platform-local-auth.mjs";
import { newOpaqueToken, tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";
import { createNativeOAuthPolicy } from "../Source/lib/platform-native-oauth-policy.mjs";
import { createNativeAuthorization, decideNativeAuthorization, exchangeNativeAuthorization } from "../Source/lib/platform-native-authorization.mjs";
import { issueNativeSession, withNativeSessionTransaction } from "../Source/lib/platform-native-sessions.mjs";
import { createNativeTokenHttp } from "../Source/lib/platform-native-token-http.mjs";
import { reserveNativeRequest } from "../Source/lib/platform-native-limits.mjs";
import { createNativeBrowserHttp } from "../Source/lib/platform-native-browser.mjs";
import { createNativeAccountHttp } from "../Source/lib/platform-native-account-http.mjs";
import { createNativeMarketplaceHttp } from "../Source/lib/platform-native-marketplace-http.mjs";

/** Reuses the caller's connection-local identity and native tables, never the real account/session tables. */
export async function verifyNativeAuthorization(pool, connection, signer, userId) {
    const now = Date.now();
    const [[account]] = await connection.execute("SELECT * FROM platform_accounts WHERE id=?", [userId]);
    const [factors] = await connection.execute(
        "SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified'", [userId]);
    const totp = factors.find((factor) => factor.factor_type === "totp");
    if (!totp) {
        // This fixture exercises password-only consent when no TOTP remains after the recovery tests.
        await connection.execute("DELETE FROM platform_mfa_factors WHERE user_id=?", [userId]);
    }
    const browserToken = newOpaqueToken();
    await connection.execute(`INSERT INTO platform_auth_sessions
        (token_hash,user_id,password_digest,assurance_level,factor_id,expires_ms) VALUES (?,?,?,?,?,?)`,
    [tokenDigest(browserToken), userId, createHash("sha256").update(account.password_hash).digest("hex"),
        totp ? 2 : 1, totp?.id ?? null, now + 3600000]);
    const authentication = await LocalPasswordAuthentication.create(pool, Buffer.alloc(32), () => now);
    const policy = createNativeOAuthPolicy({ siteOrigin: "https://qa.invalid", clientId: signer.clientId });
    const browserHttp = createNativeBrowserHttp({ pool, authentication, policy, siteOrigin: "https://qa.invalid" });
    const parameters = new URLSearchParams({
        response_type: "code", client_id: policy.clientId, redirect_uri: policy.redirectUri,
        scope: "openid email profile", state: "s".repeat(43), nonce: "n".repeat(43),
        code_challenge: "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", code_challenge_method: "S256",
    });
    const issue = (transaction, context) => issueNativeSession(transaction, signer, context);
    const exchange = (code, verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", issuer = issue) =>
        exchangeNativeAuthorization(pool, policy, {
            code, verifier, clientId: policy.clientId, redirectUri: policy.redirectUri,
        }, issuer, () => now);
    async function approve() {
        const started = await browserHttp(new Request(`https://qa.invalid/auth/v1/oauth/authorize?${parameters}`), "browser-fixture");
        assert.equal(started.status, 303);
        const consentUrl = new URL(started.headers.get("location"), "https://qa.invalid");
        const requestToken = consentUrl.searchParams.get("authorization_id");
        const anonymous = await browserHttp(new Request(consentUrl), "browser-fixture");
        assert.equal(anonymous.status, 303);
        assert.ok(anonymous.headers.get("location").startsWith("/account/sign-in/?next="));
        const cookie = `__Host-keire-session=${browserToken}`;
        const consent = await browserHttp(new Request(consentUrl, { headers: { cookie } }), "browser-fixture", async (view) => {
            assert.equal(view.session.userId, userId);
            assert.equal(view.nativeConsent.authorization_id, requestToken);
            return new Response("consent fixture");
        });
        assert.equal(consent.status, 200);
        assert.equal(consent.headers.get("referrer-policy"), "no-referrer");
        const decision = (origin) => new Request("https://qa.invalid/oauth/decision/", {
            method: "POST", headers: { origin, cookie, "content-type": "application/x-www-form-urlencoded" },
            body: new URLSearchParams({ authorization_id: requestToken, decision: "approve" }).toString(),
        });
        assert.equal((await browserHttp(decision("https://other.invalid"), "browser-fixture")).status, 403);
        const approved = await browserHttp(decision("https://qa.invalid"), "browser-fixture");
        assert.equal(approved.status, 303);
        const redirect = new URL(approved.headers.get("location"));
        assert.equal(redirect.searchParams.get("state"), "s".repeat(43));
        return { requestToken, code: redirect.searchParams.get("code") };
    }
    try {
        assert.equal((await authentication.session(browserToken)).userId, userId);
        const { requestToken, code } = await approve();
        await assert.rejects(exchange(code, "x".repeat(43)), { code: "invalid_grant" });
        const [[before]] = await connection.execute("SELECT COUNT(*) AS total FROM platform_native_sessions");
        await assert.rejects(exchange(code, undefined, async (transaction, context) => {
            await issue(transaction, context);
            throw new Error("Synthetic code exchange failure");
        }), /Synthetic code exchange failure/);
        const [[afterFailure]] = await connection.execute("SELECT COUNT(*) AS total FROM platform_native_sessions");
        assert.equal(afterFailure.total, before.total);
        const [[unused]] = await connection.execute(
            "SELECT consumed FROM platform_native_authorizations WHERE code_hash=?", [tokenDigest(code)]);
        assert.equal(Number(unused.consumed), 0);
        const tokens = await exchange(code);
        assert.equal(await withNativeSessionTransaction(pool, tokens.access_token,
            (_transaction, actor) => actor.userId, () => now), userId);
        await assert.rejects(exchange(code), { code: "invalid_grant" });
        await assert.rejects(decideNativeAuthorization(authentication, browserToken,
            policy, requestToken, true, () => now), { code: "invalid_grant" });
        const tokenHttp = createNativeTokenHttp({ pool, policy, signer });
        const approved = await approve();
        const form = new URLSearchParams({
            grant_type: "authorization_code", client_id: policy.clientId, code: approved.code,
            redirect_uri: policy.redirectUri, code_verifier: "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk",
        });
        const post = (body) => tokenHttp(new Request("https://qa.invalid/auth/v1/oauth/token", {
            method: "POST", headers: { "content-type": "application/x-www-form-urlencoded" }, body: body.toString(),
        }), "isolated-native-http");
        const exchanged = await post(form);
        assert.equal(exchanged.status, 200);
        const httpTokens = await exchanged.json();
        const marketplaceHttp = createNativeMarketplaceHttp({ pool, siteOrigin: "https://qa.invalid", clientId: policy.clientId });
        const registerDevice = (deviceName) => marketplaceHttp(new Request("https://qa.invalid/functions/v1/marketplace-hub", {
            method: "POST", headers: { authorization: `Bearer ${httpTokens.access_token}`, "content-type": "application/json" },
            body: JSON.stringify({ operation: "session.register", deviceName }),
        }));
        const registered = await registerDevice("Fixture desktop");
        assert.equal(registered.status, 201);
        const device = (await registered.json()).data;
        assert.deepEqual((await (await registerDevice("Renamed fixture")).json()).data, device);
        const [[storedDevice]] = await connection.execute("SELECT * FROM oauth_device_sessions WHERE id=?", [device.deviceSessionId]);
        assert.equal(storedDevice.oauth_session_id, `mysql:${device.oauthSessionId}`);
        assert.equal(storedDevice.device_name, "Renamed fixture");
        await connection.execute("UPDATE oauth_device_sessions SET revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [device.deviceSessionId]);
        assert.equal((await registerDevice("Cannot revive")).status, 403);
        const accountHttp = createNativeAccountHttp({ pool, siteOrigin: "https://qa.invalid", clientId: policy.clientId });
        const accountRequest = (accessToken, logout = false) => accountHttp(new Request(
            `https://qa.invalid/auth/v1/${logout ? "logout?scope=local" : "user"}`, {
                method: logout ? "POST" : "GET", headers: { authorization: `Bearer ${accessToken}` },
            }));
        const accountResponse = await accountRequest(httpTokens.access_token);
        assert.equal(accountResponse.status, 200);
        assert.deepEqual(await accountResponse.json(), {
            id: userId, email: account.email, email_confirmed_at: account.email_confirmed_at,
        });
        const profileRead = (target) => accountHttp(new Request(
            `https://qa.invalid/rest/v1/profiles?select=display_name,avatar_url&user_id=eq.${target}&limit=1`,
            { headers: { authorization: `Bearer ${httpTokens.access_token}` } }));
        const profileSave = (target, name) => accountHttp(new Request(
            "https://qa.invalid/rest/v1/profiles?on_conflict=user_id", {
                method: "POST", headers: { authorization: `Bearer ${httpTokens.access_token}`, "content-type": "application/json" },
                body: JSON.stringify({ user_id: target, display_name: name }),
            }));
        assert.equal((await profileRead("other-user")).status, 403);
        assert.equal((await profileSave("other-user", "Unauthorized")).status, 403);
        assert.equal((await profileSave(userId, "Updated native fixture")).status, 204);
        assert.deepEqual(await (await profileRead(userId)).json(), [{ display_name: "Updated native fixture", avatar_url: null }]);
        const [[savedProfile]] = await connection.execute("SELECT display_name FROM platform_profiles WHERE user_id=?", [userId]);
        assert.equal(savedProfile.display_name, "Updated native fixture");
        assert.equal((await profileSave(userId, "é".repeat(33))).status, 400);
        await connection.execute("DELETE FROM platform_profiles WHERE user_id=?", [userId]);
        assert.equal((await profileSave(userId, "Must roll back")).status, 503);
        assert.deepEqual(await (await profileRead(userId)).json(), [{ display_name: "Updated native fixture", avatar_url: null }]);
        assert.equal(await readNative(httpTokens.access_token), userId);
        assert.equal((await post(form)).status, 400);
        const refresh = new URLSearchParams({
            grant_type: "refresh_token", client_id: policy.clientId, refresh_token: httpTokens.refresh_token,
        });
        const refreshed = await post(refresh);
        assert.equal(refreshed.status, 200);
        const rotated = await refreshed.json();
        assert.equal((await post(refresh)).status, 400);
        assert.equal(await readNative(rotated.access_token), null);
        assert.equal((await accountRequest(rotated.access_token)).status, 401);
        for (let attempt = 0; attempt < 20; attempt++) {
            await reserveNativeRequest(pool, "isolated-limit-client", "authorize", now);
        }
        await assert.rejects(reserveNativeRequest(pool, "isolated-limit-client", "authorize", now),
            { code: "slow_down", status: 429 });
        await reserveNativeRequest(pool, "isolated-limit-client", "authorize", now + 600000);
        const pending = await approve();
        await authentication.signOut(browserToken);
        await assert.rejects(exchange(pending.code), { code: "invalid_grant" });
        // Browser logout cancels pending consent codes, but does not sign out the already-created desktop session.
        assert.equal(await withNativeSessionTransaction(pool, tokens.access_token,
            (_transaction, actor) => actor.userId, () => now), userId);
        assert.equal((await accountRequest(tokens.access_token, true)).status, 204);
        assert.equal((await accountRequest(tokens.access_token)).status, 401);
        assert.equal(await readNative(tokens.access_token), null);
        return 47;
    } finally {
        authentication.close();
    }

    async function readNative(accessToken) {
        return withNativeSessionTransaction(pool, accessToken, (_transaction, actor) => actor.userId, () => Date.now());
    }
}
