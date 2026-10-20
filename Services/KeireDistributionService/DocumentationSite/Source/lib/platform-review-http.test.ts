import { describe, expect, it, vi } from "vitest";
import { handleLocalReviewRequest } from "./platform-review-http.mjs";

const origin = "https://platform.invalid";
const address = `${origin}/admin/marketplace/v1/actions/`;
const cookie = `__Host-keire-session=${"a".repeat(43)}`;

describe("local staff action boundary", () => {
    it.each([
        ["GET", { origin, cookie }, 405],
        ["POST", { cookie }, 403],
        ["POST", { origin: "https://other.invalid", cookie }, 403],
        ["POST", { origin }, 401],
        ["POST", { origin, cookie, authorization: "Bearer fixture" }, 401],
    ])("rejects unsafe %s requests before accessing staff data", async (method, headers, expected) => {
        const authentication = { withSessionTransaction: vi.fn() };
        const response = await handleLocalReviewRequest(authentication, new Request(address, { method, headers }), origin, "qa");
        expect(response.status).toBe(expected);
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
    });

    it.each([
        ["{", 400],
        [JSON.stringify({ operation: "unknown" }), 503],
        [" ".repeat(65537), 400],
    ])("bounds and validates action bodies", async (body, expected) => {
        const authentication = { withSessionTransaction: vi.fn() };
        const response = await handleLocalReviewRequest(authentication, new Request(address, {
            method: "POST", headers: { origin, cookie, "content-type": "application/json" }, body,
        }), origin, "qa");
        expect(response.status).toBe(expected);
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
    });
});
