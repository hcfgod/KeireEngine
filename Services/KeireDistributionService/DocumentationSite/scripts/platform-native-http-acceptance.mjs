import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { createLocalJWKSet, jwtVerify } from "jose";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

/** Exercises the built HTTPS application using only the caller's disposable HTTP account. */
export async function verifyNativeHttp({ pool, userId, origin, clientIdentity, browserRequest, setStage }) {
    const address = new URL(origin);
    assert.equal(address.protocol, "https:");
    assert.equal(address.hostname, "localhost");
    const [[account]] = await pool.execute("SELECT email_key FROM platform_accounts WHERE id=?", [userId]);
    assert.equal(account?.email_key, `http-qa-${userId}@invalid.example`);
    const [[existing]] = await pool.execute("SELECT COUNT(*) AS total FROM platform_native_sessions WHERE user_id=?", [userId]);
    assert.equal(Number(existing.total), 0);
    const clientId = "keire-hub-desktop";
    const nonce = randomBytes(32).toString("base64url");
    const state = randomBytes(32).toString("base64url");
    const verifier = randomBytes(64).toString("base64url");
    const redirectUri = `${origin}/oauth/hub/callback/`;
    const request = (route, options = {}) => fetch(origin + route, {
        ...options, redirect: "manual", signal: AbortSignal.timeout(15000),
    });
    const postToken = (parameters) => request("/auth/v1/oauth/token", {
        method: "POST", headers: { "content-type": "application/x-www-form-urlencoded" },
        body: new URLSearchParams(parameters),
    });
    try {
        setStage("authorize");
        const parameters = new URLSearchParams({
            response_type: "code", client_id: clientId, redirect_uri: redirectUri,
            scope: "openid email profile", state, nonce,
            code_challenge: createHash("sha256").update(verifier).digest("base64url"), code_challenge_method: "S256",
        });
        const authorization = await browserRequest(`/auth/v1/oauth/authorize?${parameters}`);
        assert.equal(authorization.status, 303);
        const consentUrl = new URL(authorization.headers.get("location"), origin);
        assert.equal(consentUrl.origin, origin);
        const requestToken = consentUrl.searchParams.get("authorization_id");
        assert.match(requestToken, /^[A-Za-z0-9_-]{43}$/);
        setStage("consent-page");
        const consent = await browserRequest(consentUrl.pathname + consentUrl.search);
        assert.equal(consent.status, 200);
        assert.match(await consent.text(), /Authorize Kéire Hub/);
        assert.equal(consent.headers.get("referrer-policy"), "no-referrer");
        setStage("consent-decision");
        const approved = await browserRequest("/oauth/decision/", new URLSearchParams({
            authorization_id: requestToken, decision: "approve",
        }), { "content-type": "application/x-www-form-urlencoded" });
        assert.equal(approved.status, 303);
        const callback = new URL(approved.headers.get("location"));
        assert.equal(callback.origin + callback.pathname, redirectUri);
        assert.equal(callback.searchParams.get("state"), state);
        const code = callback.searchParams.get("code");
        assert.match(code, /^[A-Za-z0-9_-]{43}$/);
        setStage("exchange");
        const exchangeForm = { grant_type: "authorization_code", client_id: clientId,
            redirect_uri: redirectUri, code, code_verifier: verifier };
        const exchanged = await postToken(exchangeForm);
        assert.equal(exchanged.status, 200);
        const tokens = await exchanged.json();
        const keysResponse = await request("/auth/v1/.well-known/jwks.json");
        assert.equal(keysResponse.status, 200);
        const keys = createLocalJWKSet(await keysResponse.json());
        async function verifyIdentity(result) {
            const verified = await jwtVerify(result.id_token, keys, {
                issuer: `${origin}/auth/v1`, audience: clientId, algorithms: ["EdDSA"],
            });
            assert.equal(verified.payload.sub, userId);
            assert.equal(verified.payload.nonce, nonce);
            assert.equal(verified.payload.acr, "urn:keire:assurance:2");
            assert.equal(verified.payload.at_hash, createHash("sha512").update(result.access_token)
                .digest().subarray(0, 32).toString("base64url"));
        }
        await verifyIdentity(tokens);
        setStage("account");
        const user = await request("/auth/v1/user", { headers: { authorization: `Bearer ${tokens.access_token}` } });
        assert.equal(user.status, 200);
        assert.equal((await user.json()).id, userId);
        setStage("profile");
        const profile = await request(`/rest/v1/profiles?select=display_name%2Cavatar_url&user_id=eq.${userId}&limit=1`, {
            headers: { authorization: `Bearer ${tokens.access_token}` },
        });
        assert.equal(profile.status, 200);
        assert.equal((await profile.json()).length, 1);
        setStage("device-registration");
        const registerDevice = () => request("/functions/v1/marketplace-hub", {
            method: "POST", headers: { authorization: `Bearer ${tokens.access_token}`, "content-type": "application/json" },
            body: JSON.stringify({ operation: "session.register", deviceName: "Isolated native HTTPS QA" }),
        });
        const device = await registerDevice();
        assert.equal(device.status, 201);
        const deviceData = (await device.json()).data;
        assert.match(deviceData.deviceSessionId, /^[0-9a-f-]{36}$/);
        const repeatedDevice = await registerDevice();
        assert.equal(repeatedDevice.status, 201);
        assert.equal((await repeatedDevice.json()).data.deviceSessionId, deviceData.deviceSessionId);
        const replay = await postToken(exchangeForm);
        assert.equal(replay.status, 400);
        await replay.arrayBuffer();
        setStage("refresh");
        const refreshed = await postToken({ grant_type: "refresh_token", client_id: clientId,
            refresh_token: tokens.refresh_token });
        assert.equal(refreshed.status, 200);
        const replacement = await refreshed.json();
        assert.notEqual(replacement.refresh_token, tokens.refresh_token);
        await verifyIdentity(replacement);
        setStage("logout");
        const headers = { authorization: `Bearer ${replacement.access_token}` };
        const logout = await request("/auth/v1/logout?scope=local", { method: "POST", headers });
        assert.equal(logout.status, 204);
        const revoked = await request("/auth/v1/user", { headers });
        assert.equal(revoked.status, 401);
        await revoked.arrayBuffer();
        return 37;
    } finally {
        await platformTransaction(pool, async (connection) => {
            await connection.execute(`DELETE device FROM oauth_device_sessions device
                INNER JOIN platform_native_sessions session ON device.oauth_session_id=CONCAT('mysql:',session.id)
                WHERE session.user_id=? AND device.user_id=?`, [userId, userId]);
            for (const table of ["platform_native_access_tokens", "platform_native_refresh_tokens"]) {
                await connection.execute(`DELETE token FROM ${table} token
                    INNER JOIN platform_native_sessions session ON session.id=token.session_id WHERE session.user_id=?`, [userId]);
            }
            await connection.execute("DELETE FROM platform_native_sessions WHERE user_id=?", [userId]);
            await connection.execute(`DELETE FROM platform_native_authorizations
                WHERE nonce=? AND client_id=? AND (user_id IS NULL OR user_id=?)`, [nonce, clientId, userId]);
            for (const operation of ["authorize", "token"]) {
                const hash = createHash("sha256").update(`Keire.NativeOAuth.Rate.v1\0${operation}\0${clientIdentity}`).digest("hex");
                await connection.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?", [hash]);
            }
        });
    }
}
