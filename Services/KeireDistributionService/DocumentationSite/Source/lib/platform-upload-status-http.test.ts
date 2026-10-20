import { describe, expect, it, vi } from "vitest";
import { handleLocalPlatformRequest } from "./platform-request.mjs";

const origin = "https://qa.invalid.example";
const uploadId = "11111111-1111-4111-8111-111111111111";
const cookie = `__Host-keire-session=${"a".repeat(43)}`;

function fixture(withSessionTransaction = vi.fn()) {
    const render = vi.fn();
    const backend = { provider: "mysql", withBackend: async (action) => action({ authentication: { withSessionTransaction } }) };
    const request = (headers = {}, id = uploadId) => handleLocalPlatformRequest({
        backend, request: new Request(`${origin}/publisher/v1/uploads/${id}/`, { headers }),
        siteOrigin: origin, correlationId: "qa", clientIdentity: "qa", render,
    });
    return { request, render, withSessionTransaction };
}

describe("local upload status HTTP routing", () => {
    it("rejects anonymous and bearer requests without accessing data", async () => {
        const test = fixture();
        for (const headers of [{}, { cookie, authorization: "Bearer unsupported" }]) {
            const response = await test.request(headers);
            expect(response.status).toBe(401);
            expect(response.headers.get("cache-control")).toBe("private, no-store");
        }
        expect(test.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("rejects invalid identifiers and insufficient assurance", async () => {
        const execute = vi.fn();
        const test = fixture(vi.fn(async (_token, action) => action({ execute }, { assurance: 1 })));
        expect((await test.request({ cookie }, "-".repeat(36))).status).toBe(400);
        expect((await test.request({ cookie })).status).toBe(403);
        expect(execute).not.toHaveBeenCalled();
    });

    it("returns scoped status through the application adapter", async () => {
        const execute = vi.fn(async (sql) => {
            if (sql.includes("platform_feature_flags")) {
                return [[{ enabled: 1 }]];
            }
            if (sql.includes("SELECT upload.id")) {
                return [[{ id: uploadId, version_id: "version", state: "uploading",
                    version_state: "draft", validation_attempts: 0 }]];
            }
            return [[]];
        });
        const test = fixture(vi.fn(async (_token, action) => action({ execute }, { assurance: 2, userId: "owner" })));
        const response = await test.request({ cookie });
        expect(response.status).toBe(200);
        expect((await response.json()).data).toMatchObject({ uploadId, state: "uploading", validation: null });
        expect(test.render).not.toHaveBeenCalled();
    });

    it("does not expose private database errors", async () => {
        const test = fixture(vi.fn().mockRejectedValue(new Error("secret database connection")));
        const response = await test.request({ cookie });
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("secret");
    });
});
