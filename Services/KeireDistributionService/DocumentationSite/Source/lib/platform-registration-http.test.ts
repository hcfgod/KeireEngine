import { describe, expect, it, vi } from "vitest";
import { handleLocalRegistrationRequest } from "./platform-registration-http.mjs";

const origin = "https://qa.invalid.example";

describe("registration HTTP boundary", () => {
    it("rejects unsafe methods, origins, bearer tokens and unavailable delivery", async () => {
        const auth = { pool: { execute: vi.fn() }, reserveAttempt: vi.fn() };
        for (const [method, headers, worker, status] of [
            ["GET", {}, {}, 405],
            ["POST", { origin: "https://other.invalid" }, {}, 403],
            ["POST", { origin, authorization: "Bearer token" }, {}, 401],
            ["POST", { origin }, null, 503],
        ] as const) {
            const response = await handleLocalRegistrationRequest({ authentication: auth, registrationWorker: worker },
                new Request(`${origin}/account/registration/`, { method, headers }), origin, "qa", "correlation");
            expect(response.status).toBe(status);
            expect(response.headers.get("cache-control")).toBe("private, no-store");
        }
        expect(auth.pool.execute).not.toHaveBeenCalled();
        expect(auth.reserveAttempt).not.toHaveBeenCalled();
    });

    it("exchanges a valid link for a scoped HttpOnly cookie without exposing account details", async () => {
        const token = "a".repeat(43);
        const service = {
            registrationWorker: {},
            authentication: {
                now: () => 1000,
                pool: { execute: vi.fn().mockResolvedValue([[{
                    delivery_state: "sent", used: false, attempts: 0, created_ms: 500, expires_ms: 61000,
                }]]) },
            },
        };
        const request = new Request(`${origin}/account/registration/session/`, {
            method: "POST", headers: { origin, "content-type": "application/json" }, body: JSON.stringify({ token }),
        });
        const response = await handleLocalRegistrationRequest(service, request, origin, "qa", "correlation");
        expect(response.status).toBe(200);
        expect(await response.json()).toEqual({ data: { expiresAt: 61000 } });
        expect(response.headers.get("set-cookie")).toBe(
            `__Host-keire-registration=${token}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=60`);
    });

    it("clears a previous confirmation cookie when a new link is invalid", async () => {
        const service = { registrationWorker: {}, authentication: { now: () => 1000 } };
        const request = new Request(`${origin}/account/registration/session/`, {
            method: "POST", headers: { origin, "content-type": "application/json" }, body: JSON.stringify({ token: "invalid" }),
        });
        const response = await handleLocalRegistrationRequest(service, request, origin, "qa", "correlation");
        expect(response.status).toBe(400);
        expect(response.headers.get("set-cookie")).toContain("Max-Age=0");
    });
});
