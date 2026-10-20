import { describe, expect, it, vi } from "vitest";
import { handleLocalPlatformRequest } from "./platform-request.mjs";
import { LocalUploadInputError } from "./platform-upload-input.mjs";

const origin = "https://qa.invalid.example";
const uploadId = "11111111-1111-4111-8111-111111111111";
const cookie = `__Host-keire-session=${"a".repeat(43)}`;

function fixture(enabled = true) {
    const uploads = {
        reserve: vi.fn(async () => ({ uploadId })),
        prepare: vi.fn(async () => ({ uploadId, offset: 3 })),
        complete: vi.fn(async () => ({ uploadId, state: "uploaded" })),
        handle: vi.fn(async () => new Response(null, { status: 204 })),
    };
    const backend = { provider: "mysql", withBackend: async (action) => action({ uploads: enabled ? uploads : null }) };
    const send = (route, { method = "POST", headers = {}, body = "{}" } = {}) => handleLocalPlatformRequest({
        backend, siteOrigin: origin, correlationId: "qa", clientIdentity: "qa", render: vi.fn(),
        request: new Request(origin + route, { method, headers: { origin, cookie, "content-type": "application/json", ...headers },
            body: ["GET", "HEAD"].includes(method) ? undefined : body }),
    });
    return { uploads, send };
}

describe("MySQL upload HTTP operations", () => {
    it("fails closed without configured storage", async () => {
        const test = fixture(false);
        expect((await test.send("/publisher/v1/uploads/")).status).toBe(503);
        expect(test.uploads.reserve).not.toHaveBeenCalled();
    });

    it("enforces methods, origin, cookie and body limits before mutation", async () => {
        const test = fixture();
        for (const [options, status] of [
            [{ method: "GET" }, 405],
            [{ headers: { origin: "https://other.invalid" } }, 403],
            [{ headers: { cookie: "" } }, 401],
            [{ headers: { authorization: "Bearer unsupported" } }, 401],
            [{ body: "null" }, 400],
            [{ body: JSON.stringify({ text: "a".repeat(262144) }) }, 400],
        ] as const) {
            expect((await test.send("/publisher/v1/uploads/", options)).status).toBe(status);
        }
        expect(test.uploads.reserve).not.toHaveBeenCalled();
    });

    it("routes reserve, resume setup, completion and raw transfer separately", async () => {
        const test = fixture();
        expect((await test.send("/publisher/v1/uploads/")).status).toBe(201);
        expect((await test.send(`/publisher/v1/uploads/${uploadId}/prepare/`)).status).toBe(200);
        expect((await test.send(`/publisher/v1/uploads/${uploadId}/complete/`)).status).toBe(200);
        expect(test.uploads.prepare).toHaveBeenCalledWith("a".repeat(43), uploadId);
        expect(test.uploads.complete).toHaveBeenCalledWith("a".repeat(43), uploadId);
        expect((await test.send(`/publisher/v1/transfer/${"a".repeat(32)}`,
            { method: "PATCH", body: "raw bytes" })).status).toBe(204);
        expect(await test.uploads.handle.mock.calls[0][0].text()).toBe("raw bytes");
    });

    it("returns validation feedback but hides uncertain internal failures", async () => {
        const test = fixture();
        test.uploads.reserve.mockRejectedValueOnce(new LocalUploadInputError("version"));
        expect((await test.send("/publisher/v1/uploads/")).status).toBe(400);
        test.uploads.reserve.mockRejectedValueOnce(new Error("private database"));
        const response = await test.send("/publisher/v1/uploads/");
        expect(response.status).toBe(503);
        expect(response.headers.get("cache-control")).toBe("private, no-store");
        expect(await response.text()).not.toContain("private database");
    });
});
