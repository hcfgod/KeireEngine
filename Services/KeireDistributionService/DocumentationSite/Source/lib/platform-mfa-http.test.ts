import { describe, expect, it, vi } from "vitest";
import { handleLocalMfaRequest } from "./platform-mfa-http.mjs";

describe("authenticator management HTTP boundary", () => {
    const origin = "https://qa.invalid.example";
    it("rejects unsafe requests before touching authentication", async () => {
        const auth = { session: vi.fn(), withSessionTransaction: vi.fn() };
        for (const [method, headers, expected] of [
            ["GET", {}, 405],
            ["POST", { origin: "https://other.invalid" }, 403],
            ["POST", { origin, authorization: "Bearer unsupported" }, 401],
            ["POST", { origin }, 401],
        ] as const) {
            const response = await handleLocalMfaRequest(auth,
                new Request(`${origin}/account/mfa/enroll/`, { method, headers }), origin, "qa", "correlation");
            expect(response.status).toBe(expected);
            expect(response.headers.get("cache-control")).toBe("private, no-store");
        }
        expect(auth.session).not.toHaveBeenCalled();
        expect(auth.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("rejects expired sessions without exposing a token", async () => {
        const auth = { withSessionTransaction: async () => null };
        const request = new Request(`${origin}/account/mfa/remove/`, {
            method: "POST", headers: { origin, "content-type": "application/json", cookie: `__Host-keire-session=${"a".repeat(43)}` },
            body: JSON.stringify({ factorId: "00000000-0000-0000-0000-000000000001" }),
        });
        const response = await handleLocalMfaRequest(auth, request, origin, "qa", "correlation");
        expect(response.status).toBe(401);
        expect(response.headers.has("set-cookie")).toBe(false);
    });
});
