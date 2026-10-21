import { beforeEach, describe, expect, it, vi } from "vitest";
import { withNativeSessionTransaction } from "./platform-native-sessions.mjs";
import { createNativeAccountHttp } from "./platform-native-account-http.mjs";

vi.mock("./platform-native-sessions.mjs", () => ({ withNativeSessionTransaction: vi.fn() }));
const origin = "https://platform.invalid";
const clientId = "keire-hub-desktop";
const execute = vi.fn();
const identity = { clientId, userId: "user", sessionId: "session", scope: "openid email profile",
    account: { email: "fixture@invalid.example", email_confirmed_at: "2026-01-01", password_hash: "private" } };
const handle = createNativeAccountHttp({ pool: {}, siteOrigin: origin, clientId });
const request = (pathname = "/auth/v1/user", headers = {}, method = "GET") => new Request(origin + pathname, {
    method, headers: { authorization: `Bearer ${"a".repeat(43)}`, ...headers },
});

beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(withNativeSessionTransaction).mockImplementation(async (_pool, _token, action) =>
        action({ execute }, identity));
});

describe("native account HTTP", () => {
    it("reads only the authenticated profile with the bounded Hub query", async () => {
        execute.mockResolvedValue([[{ display_name: "Fixture", avatar_url: null }]]);
        const response = await handle(request("/rest/v1/profiles?select=display_name,avatar_url&user_id=eq.user&limit=1"));
        expect(await response.json()).toEqual([{ display_name: "Fixture", avatar_url: null }]);
        expect(execute).toHaveBeenCalledWith("SELECT display_name,avatar_url FROM profiles WHERE user_id=?", ["user"]);
        expect((await handle(request("/rest/v1/profiles?select=display_name,avatar_url&user_id=eq.other&limit=1"))).status).toBe(403);
    });

    it("updates both profile representations without accepting another account", async () => {
        execute.mockResolvedValue([{ affectedRows: 1 }]);
        const save = (userId, name) => handle(new Request(origin + "/rest/v1/profiles?on_conflict=user_id", {
            method: "POST", headers: { authorization: `Bearer ${"a".repeat(43)}`, "content-type": "application/json" },
            body: JSON.stringify({ user_id: userId, display_name: name }),
        }));
        expect((await save("other", "Fixture")).status).toBe(403);
        expect(execute).not.toHaveBeenCalled();
        expect((await save("user", "Fixture")).status).toBe(204);
        expect(execute).toHaveBeenCalledTimes(2);
        expect((await save("user", "\ninvalid")).status).toBe(400);
        expect((await save("user", "é".repeat(33))).status).toBe(400);
    });

    it("returns only the account fields consumed by the Hub", async () => {
        const response = await handle(request());
        expect(response.status).toBe(200);
        expect(response.headers.get("cache-control")).toContain("no-store");
        expect(await response.json()).toEqual({ id: "user", email: identity.account.email,
            email_confirmed_at: identity.account.email_confirmed_at });
    });

    it.each([{ cookie: "session=x" }, { origin }, { apikey: "legacy" }, { authorization: "Bearer invalid" }])
        ("rejects unsupported credentials before touching the database", async (headers) => {
            expect((await handle(request("/auth/v1/user", headers))).status).toBeGreaterThanOrEqual(400);
            expect(withNativeSessionTransaction).not.toHaveBeenCalled();
        });

    it("returns 401 for expired or revoked access", async () => {
        vi.mocked(withNativeSessionTransaction).mockResolvedValue(null);
        expect((await handle(request())).status).toBe(401);
    });

    it("requires email scope and the registered client", async () => {
        for (const [change, status] of [[{ scope: "openid" }, 403], [{ clientId: "other-client" }, 401]] as const) {
            vi.mocked(withNativeSessionTransaction).mockImplementation(async (_pool, _token, action) =>
                action({ execute }, { ...identity, ...change }));
            expect((await handle(request())).status).toBe(status);
        }
    });

    it("revokes only the authenticated native session on local logout", async () => {
        expect((await handle(request("/auth/v1/logout?scope=local", {}, "POST"))).status).toBe(204);
        expect(execute).toHaveBeenCalledWith("UPDATE platform_native_sessions SET revoked_ms=? WHERE id=?",
            [expect.any(Number), "session"]);
    });

    it("rejects global logout and arbitrary queries", async () => {
        expect((await handle(request("/auth/v1/logout?scope=global", {}, "POST"))).status).toBe(400);
        expect((await handle(request("/auth/v1/user?user_id=other"))).status).toBe(400);
        expect(withNativeSessionTransaction).not.toHaveBeenCalled();
    });

    it("redacts internal database errors", async () => {
        vi.mocked(withNativeSessionTransaction).mockRejectedValue(new Error("private database details"));
        const response = await handle(request());
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("private database");
    });
});
