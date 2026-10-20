import { describe, expect, it, vi } from "vitest";
import { handleLocalVersionSubmit } from "./platform-version-submit-http.mjs";

const origin = "https://platform.invalid";
const versionId = "11111111-1111-4111-8111-111111111111";
const address = `${origin}/publisher/v1/versions/${versionId}/submit/`;
const cookie = `__Host-keire-session=${"a".repeat(43)}`;

describe("publisher submission HTTP boundary", () => {
    it.each([
        ["GET", { origin, cookie }, 405],
        ["POST", { cookie }, 403],
        ["POST", { origin: "https://other.invalid", cookie }, 403],
        ["POST", { origin }, 401],
        ["POST", { origin, cookie, authorization: "Bearer fixture" }, 401],
    ])("rejects invalid %s requests before database access", async (method, headers, status) => {
        const authentication = { withSessionTransaction: vi.fn() };
        const response = await handleLocalVersionSubmit(authentication, new Request(address, { method, headers }), origin, "qa");
        expect(response.status).toBe(status);
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
        expect(response.headers.get("cache-control")).toBe("private, no-store");
    });

    it("returns the existing publisher contract on success", async () => {
        const data = { submissionId: "fixture", state: "submitted" };
        const authentication = { withSessionTransaction: vi.fn(async () => data) };
        const response = await handleLocalVersionSubmit(authentication,
            new Request(address, { method: "POST", headers: { origin, cookie } }), origin, "qa");
        expect(response.status).toBe(201);
        expect(await response.json()).toEqual({ data, meta: { apiVersion: "publisher/v1", correlationId: "qa" } });
    });

    it("rejects revoked sessions and suppresses database details", async () => {
        const authentication = { withSessionTransaction: vi.fn(async () => null) };
        const request = () => new Request(address, { method: "POST", headers: { origin, cookie } });
        expect((await handleLocalVersionSubmit(authentication, request(), origin, "qa")).status).toBe(401);
        authentication.withSessionTransaction.mockRejectedValue(new Error("private database connection"));
        const response = await handleLocalVersionSubmit(authentication, request(), origin, "qa");
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("private database connection");
    });

    it("leaves unrelated routes alone and rejects invalid host configuration", async () => {
        const authentication = { withSessionTransaction: vi.fn() };
        expect(await handleLocalVersionSubmit(authentication, new Request(`${origin}/downloads/`), origin, "qa")).toBeNull();
        expect((await handleLocalVersionSubmit(authentication,
            new Request(address, { method: "POST", headers: { origin, cookie } }), "invalid", "qa")).status).toBe(403);
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
    });
});
