import { describe, expect, it } from "vitest";
import { createNativeOAuthPolicy, verifyNativePkce } from "./platform-native-oauth-policy.mjs";

const siteOrigin = "https://platform.invalid";
const clientId = "keire-hub-desktop";
const verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
const challenge = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";

function input() {
    return new URLSearchParams({
        response_type: "code", client_id: clientId, redirect_uri: `${siteOrigin}/oauth/hub/callback/`,
        scope: "openid email profile", state: "a".repeat(43), nonce: "b".repeat(43),
        code_challenge: challenge, code_challenge_method: "S256",
    });
}

describe("native OAuth request boundary", () => {
    const policy = createNativeOAuthPolicy({ siteOrigin, clientId });

    it("preserves the Hub's state, nonce, scopes and S256 challenge", () => {
        expect(policy.authorize(input())).toEqual({
            clientId, redirectUri: `${siteOrigin}/oauth/hub/callback/`, scope: "openid email profile",
            state: "a".repeat(43), nonce: "b".repeat(43), challenge,
        });
    });

    it.each([
        ["client_id", "another-client"], ["redirect_uri", "https://other.invalid/oauth/hub/callback/"],
        ["redirect_uri", `${siteOrigin}/oauth/hub/callback/?next=other`],
        ["redirect_uri", "keirehub://oauth/callback"], ["response_type", "token"],
        ["scope", "openid admin"], ["scope", "email profile"], ["scope", "openid openid"],
        ["state", "short"], ["nonce", "short"], ["code_challenge_method", "plain"],
        ["code_challenge", "a".repeat(42)], ["code_challenge", `${"a".repeat(42)}B`],
        ["prompt", "none"],
    ])("rejects invalid %s=%s", (field, value) => {
        const parameters = input();
        parameters.set(field, value);
        expect(() => policy.authorize(parameters)).toThrow();
    });

    it.each(["client_id", "redirect_uri", "state", "nonce", "code_challenge"])("rejects duplicate %s", (field) => {
        const parameters = input();
        parameters.append(field, parameters.get(field)!);
        expect(() => policy.authorize(parameters)).toThrow();
    });

    it("verifies the fixed S256 interoperability vector and rejects wrong verifiers", () => {
        expect(verifyNativePkce(challenge, verifier)).toBe(true);
        expect(verifyNativePkce(challenge, `${verifier.slice(0, -1)}A`)).toBe(false);
        expect(verifyNativePkce(challenge, "short")).toBe(false);
        expect(verifyNativePkce(challenge, "é".repeat(43))).toBe(false);
        expect(verifyNativePkce("short", verifier)).toBe(false);
    });

    it.each(["http://platform.invalid", `${siteOrigin}/path`, `${siteOrigin}/`, "https://user@platform.invalid"])(
        "rejects a noncanonical configured origin %s", (origin) => {
            expect(() => createNativeOAuthPolicy({ siteOrigin: origin, clientId })).toThrow();
        },
    );
});
