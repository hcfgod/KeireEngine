import { describe, expect, it, vi } from "vitest";
import { handleLocalPlatformRequest } from "./platform-request.mjs";

const origin = "https://qa.invalid.example";
const url = `${origin}/publisher/v1/applications/`;
const headers = { origin, "content-type": "application/json", cookie: `__Host-keire-session=${"a".repeat(43)}` };
const input = { organizationId: "11111111-1111-4111-8111-111111111111",
    publicName: "QA publisher", statement: "A synthetic publisher application for testing." };

function fixture(withSessionTransaction = vi.fn()) {
    const render = vi.fn();
    const backend = { provider: "mysql", withBackend: async (action) => action({ authentication: { withSessionTransaction } }) };
    const request = (value: Request) => handleLocalPlatformRequest({
        backend, request: value, siteOrigin: origin, correlationId: "qa", clientIdentity: "qa", render,
    });
    return { request, render, withSessionTransaction };
}

describe("publisher application browser routing", () => {
    it("rejects methods, cross-origin requests, bearer credentials and missing cookies before data access", async () => {
        const test = fixture();
        for (const [request, status] of [
            [new Request(url), 405],
            [new Request(url, { method: "POST", headers: { origin: "https://other.example" } }), 403],
            [new Request(url, { method: "POST", headers: { origin, authorization: "Bearer unsupported" } }), 401],
            [new Request(url, { method: "POST", headers: { origin } }), 401],
        ] as const) {
            const response = await test.request(request);
            expect(response.status).toBe(status);
            expect(response.headers.get("cache-control")).toBe("private, no-store");
        }
        expect(test.withSessionTransaction).not.toHaveBeenCalled();
        expect(test.render).not.toHaveBeenCalled();
    });

    it("bounds malformed bodies and preserves the AAL2 requirement", async () => {
        const execute = vi.fn();
        const test = fixture(vi.fn(async (_token, action) => action({ execute }, { assurance: 1 })));
        for (const body of ["{}", "null", "{", JSON.stringify({ data: "x".repeat(32768) })]) {
            expect((await test.request(new Request(url, { method: "POST", headers, body }))).status).toBe(400);
        }
        const response = await test.request(new Request(url, { method: "POST", headers, body: JSON.stringify(input) }));
        expect(response.status).toBe(403);
        expect(execute).not.toHaveBeenCalled();
    });

    it("does not expose database errors or claim an uncertain save succeeded", async () => {
        const test = fixture(vi.fn().mockRejectedValue(new Error("private connection details")));
        const response = await test.request(new Request(url, { method: "POST", headers, body: JSON.stringify(input) }));
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("private connection");
    });
});
