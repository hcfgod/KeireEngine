import { describe, expect, it, vi } from "vitest";
import { createNativeTokenHttp } from "./platform-native-token-http.mjs";
import { createNativeOAuthPolicy } from "./platform-native-oauth-policy.mjs";

const siteOrigin = "https://platform.invalid";
const endpoint = `${siteOrigin}/auth/v1/oauth/token`;

function fixture() {
    const policy = createNativeOAuthPolicy({ siteOrigin, clientId: "keire-hub-desktop" });
    const state = { attempts: 0 };
    const windowStart = Date.now() - 1;
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn(async (sql) => sql.includes("SELECT attempts")
            ? [[{ attempts: state.attempts, window_start: windowStart }]] : [[]]),
    };
    const pool = { getConnection: vi.fn(async () => connection) };
    const signer = { clientId: policy.clientId, issuer: `${siteOrigin}/auth/v1`, sign: vi.fn() };
    return { state, pool, connection, handle: createNativeTokenHttp({ pool, policy, signer }) };
}

function request(body = "grant_type=refresh_token&client_id=keire-hub-desktop&refresh_token=invalid", headers = {}, url = endpoint) {
    return new Request(url, {
        method: "POST", headers: { "content-type": "application/x-www-form-urlencoded", ...headers }, body,
    });
}

describe("native token HTTP boundary", () => {
    it("returns standard OAuth errors and never caches token responses", async () => {
        const { handle } = fixture();
        const response = await handle(request(), "trusted-client");
        expect(response.status).toBe(400);
        expect((await response.json()).error).toBe("invalid_grant");
        expect(response.headers.get("cache-control")).toBe("private, no-store");
        expect(response.headers.get("pragma")).toBe("no-cache");
    });

    it.each(["origin", "cookie", "authorization", "apikey"])("rejects %s before reading or accessing the database", async (name) => {
        const { handle, pool } = fixture();
        const incoming = request(undefined, { [name]: "unexpected" });
        expect((await handle(incoming, "trusted-client")).status).toBe(403);
        expect(incoming.bodyUsed).toBe(false);
        expect(pool.getConnection).not.toHaveBeenCalled();
    });

    it.each([
        "grant_type=password&username=user&password=secret",
        "grant_type=refresh_token&client_id=keire-hub-desktop",
        "grant_type=refresh_token&client_id=keire-hub-desktop&refresh_token=a&refresh_token=b",
        "grant_type=refresh_token&client_id=keire-hub-desktop&refresh_token=a&extra=value",
        "x".repeat(8193),
    ])("rejects malformed, unsupported or oversized forms", async (body) => {
        const { handle } = fixture();
        expect((await handle(request(body), "trusted-client")).status).toBe(400);
    });

    it("does not accept JSON token requests", async () => {
        const { handle } = fixture();
        expect((await handle(request("{}", { "content-type": "application/json" }), "trusted-client")).status).toBe(400);
    });

    it("enforces request limits before reading token credentials", async () => {
        const { handle, state } = fixture();
        state.attempts = 60;
        const incoming = request();
        const response = await handle(incoming, "trusted-client");
        expect(response.status).toBe(429);
        expect(response.headers.get("retry-after")).toBe("60");
        expect(incoming.bodyUsed).toBe(false);
    });

    it("requires trusted client context and suppresses database diagnostics", async () => {
        const { handle, pool } = fixture();
        expect((await handle(request(), "")).status).toBe(400);
        pool.getConnection.mockRejectedValue(new Error("private database credential"));
        const response = await handle(request(), "trusted-client");
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("private database credential");
    });

    it("requires the configured origin, no query parameters, and POST", async () => {
        const { handle, pool } = fixture();
        expect((await handle(request(undefined, {}, endpoint.replace("platform.invalid", "other.invalid")), "client")).status).toBe(403);
        expect((await handle(request(undefined, {}, `${endpoint}?code=value`), "client")).status).toBe(403);
        expect((await handle(new Request(endpoint), "client")).status).toBe(405);
        expect(await handle(new Request(`${siteOrigin}/other/`), "client")).toBeNull();
        expect(pool.getConnection).not.toHaveBeenCalled();
    });
});
