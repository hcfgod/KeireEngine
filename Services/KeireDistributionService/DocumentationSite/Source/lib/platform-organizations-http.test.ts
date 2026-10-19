import { describe, expect, it, vi } from "vitest";
import { handleLocalOrganizationRequest } from "./platform-organizations-http.mjs";

describe("organization browser boundary", () => {
    const origin = "https://qa.invalid.example";
    const url = `${origin}/marketplace/v1/organizations/`;

    it("rejects methods, cross-origin requests, bearer credentials and missing sessions", async () => {
        const authentication = { withSessionTransaction: vi.fn() };
        const cases = [
            [new Request(url), 405],
            [new Request(url, { method: "POST", headers: { origin: "https://other.invalid" } }), 403],
            [new Request(url, { method: "POST", headers: { origin, authorization: "Bearer unsupported" } }), 401],
            [new Request(url, { method: "POST", headers: { origin } }), 401],
        ] as const;
        for (const [request, status] of cases) {
            const response = await handleLocalOrganizationRequest(authentication, request, origin, "qa");
            expect(response.status).toBe(status);
            expect(response.headers.get("cache-control")).toBe("private, no-store");
        }
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("bounds request bytes and preserves the privileged assurance requirement", async () => {
        const execute = vi.fn();
        const authentication = {
            withSessionTransaction: async (_token, action) => action({ execute }, { assurance: 1 }),
        };
        const headers = { origin, "content-type": "application/json", cookie: `__Host-keire-session=${"a".repeat(43)}` };
        const oversized = new Request(url, { method: "POST", headers, body: JSON.stringify({ data: "x".repeat(8192) }) });
        expect((await handleLocalOrganizationRequest(authentication, oversized, origin, "qa")).status).toBe(400);
        const normal = new Request(url, { method: "POST", headers, body: JSON.stringify({ slug: "example", displayName: "Example" }) });
        expect((await handleLocalOrganizationRequest(authentication, normal, origin, "qa")).status).toBe(403);
        expect(execute).not.toHaveBeenCalled();
    });
});
