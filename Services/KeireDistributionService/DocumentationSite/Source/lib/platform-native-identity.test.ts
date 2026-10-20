import { createHash, generateKeyPairSync } from "node:crypto";
import { createLocalJWKSet, jwtVerify } from "jose";
import { describe, expect, it } from "vitest";
import { createNativeIdentitySigner } from "./platform-native-identity.mjs";

function fixture() {
    const keys = generateKeyPairSync("ed25519");
    const signer = createNativeIdentitySigner({
        siteOrigin: "https://platform.invalid", clientId: "keire-hub-desktop", privateKey: keys.privateKey,
    });
    const claims = {
        userId: "11111111-1111-4111-8111-111111111111",
        sessionId: "22222222-2222-4222-8222-222222222222",
        nonce: "n".repeat(43), assurance: 1, authenticatedAt: 1800000000000, now: 1800000001000,
        scope: "openid email profile", email: "fixture@invalid.example", emailVerified: true,
        displayName: "Fixture", accessToken: "a".repeat(43),
    };
    const verify = (token, overrides = {}) => jwtVerify(token, createLocalJWKSet(signer.jwks), {
        algorithms: ["EdDSA"], issuer: signer.issuer, audience: signer.clientId,
        currentDate: new Date(claims.now), ...overrides,
    });
    return { signer, claims, verify };
}

describe("native signed identity tokens", () => {
    it("verifies account, client, nonce, session, access-token hash, and short expiry", async () => {
        const { signer, claims, verify } = fixture();
        const { payload, protectedHeader } = await verify(await signer.sign(claims));
        expect(payload.sub).toBe(claims.userId);
        expect(payload.sid).toBe(claims.sessionId);
        expect(payload.nonce).toBe(claims.nonce);
        expect(payload.auth_time).toBe(1800000000);
        expect(payload.exp! - payload.iat!).toBe(300);
        expect(payload.email_verified).toBe(true);
        expect(payload.name).toBe("Fixture");
        expect(payload.at_hash).toBe(createHash("sha512").update(claims.accessToken).digest().subarray(0, 32).toString("base64url"));
        expect(protectedHeader.kid).toBe(signer.jwks.keys[0].kid);
        expect(signer.jwks.keys[0]).not.toHaveProperty("d");
    });

    it("omits email/profile claims when their scopes were not approved", async () => {
        const { signer, claims, verify } = fixture();
        const { payload } = await verify(await signer.sign({ ...claims, scope: "openid" }));
        expect(payload).not.toHaveProperty("email");
        expect(payload).not.toHaveProperty("email_verified");
        expect(payload).not.toHaveProperty("name");
    });

    it.each([
        { audience: "another-client" }, { issuer: "https://other.invalid/auth/v1" },
        { currentDate: new Date(1800000301000) }, { algorithms: ["HS256"] },
    ])("rejects wrong verification context", async (overrides) => {
        const { signer, claims, verify } = fixture();
        await expect(verify(await signer.sign(claims), overrides)).rejects.toThrow();
    });

    it.each([
        { nonce: "short" }, { assurance: 3 }, { authenticatedAt: 1800000002000 },
        { sessionId: "bad" }, { scope: "openid admin" }, { accessToken: "bad" },
    ])("rejects invalid issuance claims", async (overrides) => {
        const { signer, claims } = fixture();
        await expect(signer.sign({ ...claims, ...overrides })).rejects.toThrow();
    });

    it("refuses signing after shutdown while retaining only public verification metadata", async () => {
        const { signer, claims } = fixture();
        signer.close();
        await expect(signer.sign(claims)).rejects.toThrow("closed");
        expect(signer.jwks.keys).toHaveLength(1);
        expect(JSON.stringify(signer.jwks)).not.toContain('"d":');
    });
});
