import { describe, expect, it, vi } from "vitest";
import { createLocalAuthenticationHttp } from "./platform-auth-http.mjs";

const origin = "https://example.test";
const token = "a".repeat(43);
function fixture() {
    const service = {
        signIn: vi.fn().mockResolvedValue({ state: "authenticated", token, userId: "user", assurance: 1 }),
        session: vi.fn().mockResolvedValue({ userId: "user", assurance: 2 }),
        completeTotp: vi.fn().mockResolvedValue({ state: "invalid_credentials" }),
        signOut: vi.fn().mockResolvedValue(undefined),
        pendingChallenge: vi.fn().mockResolvedValue(null),
    };
    return { service, handle: createLocalAuthenticationHttp(service, origin) };
}
function post(path = "/account/session", body = '{}', extra = {}) {
    return new Request(origin + path, { method: "POST", body,
        headers: { origin, "content-type": "application/json", ...extra } });
}

describe("local authentication HTTP boundary", () => {
    it("expires an unavailable challenge without producing an account session", async () => {
        const { handle, service } = fixture();
        const response = await handle(new Request(origin + "/account/mfa/challenge/status", {
            headers: { cookie: `__Host-keire-challenge=${token}` },
        }), "client", "id");
        expect(response.status).toBe(401);
        expect(service.pendingChallenge).toHaveBeenCalledWith(token);
        expect(service.session).not.toHaveBeenCalled();
        expect(response.headers.get("set-cookie")).toContain("Max-Age=0");
    });

    it("does not accept mutations on the challenge status endpoint", async () => {
        const { handle, service } = fixture();
        const response = await handle(post("/account/mfa/challenge/status"), "client", "id");
        expect(response.status).toBe(405);
        expect(response.headers.get("allow")).toBe("GET");
        expect(service.pendingChallenge).not.toHaveBeenCalled();
        expect(service.signIn).not.toHaveBeenCalled();
    });
    it("sets secure cookies without exposing session tokens in JSON", async () => {
        const { handle } = fixture();
        const response = await handle(post(), "trusted-client", "correlation");
        expect(response.status).toBe(200);
        expect(response.headers.get("set-cookie")).toContain(`__Host-keire-session=${token}`);
        expect(response.headers.get("set-cookie")).toContain("Path=/; HttpOnly; Secure; SameSite=Lax");
        expect(await response.text()).not.toContain(token);
    });

    it.each(["https://other.test", "null", ""])("rejects untrusted origins", async (value) => {
        const { handle, service } = fixture();
        expect((await handle(post(undefined, '{}', { origin: value, "x-forwarded-host": "other.test" }), "client", "id")).status).toBe(403);
        expect(service.signIn).not.toHaveBeenCalled();
    });

    it.each(['[]', 'null', '{', JSON.stringify({ payload: "x".repeat(8192) })])("bounds and validates bodies", async (body) => {
        const { handle, service } = fixture();
        expect((await handle(post(undefined, body), "client", "id")).status).toBe(400);
        expect(service.signIn).not.toHaveBeenCalled();
    });

    it("does not turn an MFA challenge into an authenticated session", async () => {
        const { handle, service } = fixture();
        service.signIn.mockResolvedValue({ state: "mfa_required", challengeToken: token, factors: [] });
        const response = await handle(post(undefined, '{}', { cookie: `__Host-keire-session=${token}` }), "client", "id");
        expect(service.signOut).toHaveBeenCalledWith(token);
        expect(response.headers.get("set-cookie")).toContain("__Host-keire-session=; ");
        expect(response.headers.get("set-cookie")).toContain(`__Host-keire-challenge=${token}`);
        expect(await response.json()).toEqual({ data: { mfaRequired: true, factors: [] } });
    });

    it("rejects ambiguous session cookies", async () => {
        const { handle, service } = fixture();
        const response = await handle(new Request(origin + "/account/session", {
            headers: { cookie: `__Host-keire-session=${token}; __Host-keire-session=${token}` },
        }), "client", "id");
        expect(await response.json()).toEqual({ data: { signedIn: false, assuranceLevel: null } });
        expect(service.session).not.toHaveBeenCalled();
    });

    it("keeps bearer authentication separate", async () => {
        const { handle, service } = fixture();
        expect((await handle(post(undefined, '{}', { authorization: `Bearer ${token}` }), "client", "id")).status).toBe(401);
        expect(service.signIn).not.toHaveBeenCalled();
    });

    it("does not expose database errors", async () => {
        const { handle, service } = fixture();
        service.signIn.mockRejectedValue(new Error("private database connection information"));
        const response = await handle(post(), "client", "id");
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("database");
    });

    it("revokes logout and preserves the existing redirect", async () => {
        const { handle, service } = fixture();
        const response = await handle(post("/account/session/delete", '{}', {
            cookie: `__Host-keire-session=${token}`,
        }), "client", "id");
        expect(service.signOut).toHaveBeenCalledWith(token);
        expect(response.status).toBe(303);
        expect(response.headers.get("location")).toBe("/");
    });

    it("uses the HttpOnly challenge cookie for MFA", async () => {
        const { handle, service } = fixture();
        const response = await handle(post("/account/mfa/challenge/verify", JSON.stringify({ factorId: "factor", code: "123456" }),
            { cookie: `__Host-keire-challenge=${token}` }), "client", "id");
        expect(response.status).toBe(401);
        expect(service.completeTotp).toHaveBeenCalledWith(token, "factor", "123456");
    });
});
