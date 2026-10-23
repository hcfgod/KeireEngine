import { createHash } from "node:crypto";
import { describe, expect, it, vi } from "vitest";
import { createGitHubProvider } from "./platform-github-provider.mjs";

const settings = () => ({ siteOrigin: "https://platform.invalid", clientId: "Iv1.fixtureclient", clientSecret: Buffer.alloc(40, 97) });
const verifier = "v".repeat(43);
const state = "s".repeat(43);
const json = (value) => new Response(JSON.stringify(value), { headers: { "content-type": "application/json" } });
function fixture(overrides = {}) {
    const values = [{ access_token: "gho_" + "a".repeat(40), token_type: "bearer" }, { id: 12345, login: "fixture-user" },
        [{ primary: true, verified: true, email: "fixture@example.invalid" }]];
    const fetcher = vi.fn(async () => json(values.shift()));
    const input = { ...settings(), clientId: "fixtureclient123", ...overrides };
    return { input, fetcher, values, provider: createGitHubProvider(input, fetcher) };
}

describe("GitHub provider transport", () => {
    it("pins endpoint, callback, scope and S256 and owns the protected secret", async () => {
        const { provider, input, fetcher } = fixture();
        input.clientSecret.fill(0);
        const url = new URL(provider.authorize(state, verifier));
        expect(url.origin + url.pathname).toBe("https://github.com/login/oauth/authorize");
        expect(url.searchParams.get("code_challenge")).toBe(createHash("sha256").update(verifier).digest("base64url"));
        expect(url.searchParams.get("code_challenge_method")).toBe("S256");
        expect(url.searchParams.get("redirect_uri")).toBe("https://platform.invalid/account/github/callback/");
        expect(await provider.identity("code123", verifier)).toEqual({ subject: "12345", login: "fixture-user", email: "fixture@example.invalid" });
        expect(new URLSearchParams(fetcher.mock.calls[0][1].body).get("client_secret")).toBe("a".repeat(40));
        expect(new URLSearchParams(fetcher.mock.calls[0][1].body).get("code_verifier")).toBe(verifier);
        expect(fetcher.mock.calls.every((call) => call[1].redirect === "error")).toBe(true);
        expect(fetcher.mock.calls[1][0]).toBe("https://api.github.com/user");
        provider.close();
        expect(() => provider.authorize(state, verifier)).toThrow("closed");
    });

    it.each(["http://platform.invalid", "https://platform.invalid/", "https://user@platform.invalid", "https://platform.invalid/path"])(
        "rejects noncanonical origin %s", (siteOrigin) => {
            expect(() => fixture({ siteOrigin })).toThrow();
        });

    it("rejects incomplete settings and invalid codes before network access", async () => {
        expect(() => fixture({ clientSecret: Buffer.alloc(4) })).toThrow("protected");
        const { provider, fetcher } = fixture();
        expect(() => provider.authorize("short", verifier)).toThrow();
        await expect(provider.identity("code with space", verifier)).rejects.toThrow();
        expect(fetcher).not.toHaveBeenCalled();
        provider.close();
    });

    it.each([
        [{ id: 1.2, login: "fixture" }, [{ primary: true, verified: true, email: "x@example.invalid" }]],
        [{ id: 12, login: "fixture" }, [{ primary: true, verified: false, email: "x@example.invalid" }]],
        [{ id: 12, login: "fixture" }, [{ primary: false, verified: true, email: "x@example.invalid" }]],
        [{ id: 12, login: "<script>" }, [{ primary: true, verified: true, email: "x@example.invalid" }]],
    ])("rejects untrusted identity or email %#", async (user, emails) => {
        const { provider, values } = fixture();
        values[1] = user;
        values[2] = emails;
        await expect(provider.identity("code123", verifier)).rejects.toThrow("could not be verified");
        provider.close();
    });

    it("does not expose provider error bodies or accept oversized responses", async () => {
        for (const response of [new Response("secret-token-body", { status: 500 }),
            new Response("a".repeat(65537), { headers: { "content-type": "application/json" } })]) {
            const provider = createGitHubProvider({ ...settings(), clientId: "fixtureclient123" }, async () => response);
            await expect(provider.identity("code123", verifier)).rejects.toThrow(/^GitHub sign-in could not be verified/);
            provider.close();
        }
    });
});
