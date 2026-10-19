import { describe, expect, it, vi } from "vitest";
import { createLocalAuthenticationHttp } from "./platform-auth-http.mjs";

const origin = "https://example.test";
const parent = "p".repeat(43);
const emailToken = "e".repeat(43);
const session = "s".repeat(43);
const paths = ["/account/email/enroll", "/account/email/enroll/verify", "/account/email/challenge", "/account/email/challenge/verify"];

function fixture() {
    const authentication = { signOut: vi.fn(), session: vi.fn() };
    const email = {
        beginEnrollment: vi.fn().mockResolvedValue({ state: "code_sent", token: emailToken, expiresAt: 1700000300000 }),
        completeEnrollment: vi.fn().mockResolvedValue({ state: "enrolled" }),
        beginSignIn: vi.fn().mockResolvedValue({ state: "code_sent", token: emailToken, expiresAt: 1700000300000 }),
        completeSignIn: vi.fn().mockResolvedValue({ state: "authenticated", token: session, userId: "user", assurance: 1 }),
    };
    return { authentication, email, handle: createLocalAuthenticationHttp(authentication, origin, email) };
}

function request(path, headers = {}, body = {}) {
    return new Request(origin + path, { method: "POST", headers: {
        origin, "content-type": "application/json", ...headers,
    }, body: JSON.stringify(body) });
}

describe("email verification HTTP boundary", () => {
    it.each(paths)("rejects cross-origin mutation for %s", async (path) => {
        const { handle, email } = fixture();
        expect((await handle(request(path, { origin: "https://other.test" }), "client", "id")).status).toBe(403);
        for (const method of Object.values(email)) {
            expect(method).not.toHaveBeenCalled();
        }
    });

    it("keeps the challenge in a secure cookie rather than JSON", async () => {
        const { handle, email } = fixture();
        const result = await handle(request(paths[2], { cookie: `__Host-keire-challenge=${parent}` },
            { factorId: "factor", token: "untrusted" }), "trusted", "id");
        expect(email.beginSignIn).toHaveBeenCalledWith(parent, "factor", "trusted");
        expect(result.status).toBe(200);
        expect(result.headers.get("set-cookie")).toContain(`${emailToken}; Path=/; HttpOnly; Secure; SameSite=Lax`);
        expect(await result.text()).not.toContain(emailToken);
        expect(result.headers.get("cache-control")).toBe("private, no-store");
    });

    it("uses separate enrollment and sign-in cookies and does not expose passwords in responses", async () => {
        const { handle, email } = fixture();
        const begin = await handle(request(paths[0], { cookie: `__Host-keire-session=${session}` },
            { password: "synthetic-password" }), "trusted", "id");
        expect(email.beginEnrollment).toHaveBeenCalledWith(session, "synthetic-password", "trusted");
        expect(begin.headers.get("set-cookie")).toContain("__Host-keire-email-enrollment=");
        expect(await begin.text()).not.toContain("synthetic-password");
        const completed = await handle(request(paths[1], { cookie:
            `__Host-keire-session=${session}; __Host-keire-email-enrollment=${emailToken}` }, { code: "12345678" }), "trusted", "id");
        expect(email.completeEnrollment).toHaveBeenCalledWith(session, emailToken, "12345678");
        expect((await completed.json()).data.emailEnabled).toBe(true);
        expect(completed.headers.get("set-cookie")).toContain("Max-Age=0");
    });

    it("consumes cookie-bound login challenges and clears them when authenticated", async () => {
        const { handle, email } = fixture();
        const result = await handle(request(paths[3], { cookie:
            `__Host-keire-challenge=${parent}; __Host-keire-email-challenge=${emailToken}` },
        { factorId: "factor", code: "12345678" }), "trusted", "id");
        expect(email.completeSignIn).toHaveBeenCalledWith(parent, "factor", emailToken, "12345678");
        expect(result.headers.get("set-cookie")).toContain(`__Host-keire-session=${session}`);
        expect(result.headers.get("set-cookie")).toContain("__Host-keire-email-challenge=;");
        expect(await result.text()).not.toContain(session);
    });

    it("returns actionable delivery and cooldown failures without granting a session", async () => {
        const { handle, email } = fixture();
        email.beginSignIn.mockResolvedValueOnce({ state: "delivery_failed" });
        const failed = await handle(request(paths[2]), "client", "id");
        expect(failed.status).toBe(503);
        expect(failed.headers.get("set-cookie")).not.toContain("__Host-keire-session=");
        email.beginSignIn.mockResolvedValueOnce({ state: "rate_limited", retryAfterSeconds: 60 });
        const limited = await handle(request(paths[2]), "client", "id");
        expect(limited.status).toBe(429);
        expect(limited.headers.get("retry-after")).toBe("60");
    });

    it("refuses endpoints without a configured email service", async () => {
        const handle = createLocalAuthenticationHttp({}, origin);
        expect((await handle(request(paths[2]), "client", "id")).status).toBe(503);
    });
});
