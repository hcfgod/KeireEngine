import { describe, expect, it, vi } from "vitest";
import { confirmRegistration } from "./platform-registration-confirmation.mjs";

describe("registration confirmation boundary", () => {
    it("rejects malformed requests before accessing the database", async () => {
        const auth = { reserveAttempt: vi.fn(), pool: { getConnection: vi.fn() } };
        for (const [token, password, client] of [
            ["bad-token", "password", "qa"],
            ["a".repeat(43), "", "qa"],
            ["a".repeat(43), "x".repeat(257), "qa"],
            ["a".repeat(43), "password", ""],
        ]) {
            expect(await confirmRegistration(auth, token, password, client)).toEqual({ state: "invalid_confirmation" });
        }
        expect(auth.reserveAttempt).not.toHaveBeenCalled();
        expect(auth.pool.getConnection).not.toHaveBeenCalled();
    });

    it("does not consume the token or open a transaction when rate limited", async () => {
        const auth = {
            now: () => 1000,
            reserveAttempt: vi.fn().mockResolvedValue(false),
            pool: { getConnection: vi.fn() },
        };
        expect(await confirmRegistration(auth, "a".repeat(43), "password", "qa")).toEqual({ state: "rate_limited" });
        expect(auth.pool.getConnection).not.toHaveBeenCalled();
    });
});
