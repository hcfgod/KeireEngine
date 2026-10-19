import { describe, expect, it, vi } from "vitest";
import { handleLocalClaimRequest } from "./platform-claims-http.mjs";

const origin = "https://example.test";
const token = "a".repeat(43);
const body = {
    productId: "00000000-0000-4000-8000-000000000001", ownership: "personal",
    licenseSpdx: "MIT", licenseRevision: "1", acceptedLicenseSnapshot: "License text",
};
function fixture(overrides = {}, data = body) {
    const request = new Request(origin + "/marketplace/v1/claims", {
        method: "POST", headers: { origin, "content-type": "application/json",
            "idempotency-key": "unique-claim-request-key", cookie: `__Host-keire-session=${token}`, ...overrides },
        body: JSON.stringify(data),
    });
    const auth = { withSessionTransaction: vi.fn().mockResolvedValue({ entitlementId: "owned", organizationId: null }) };
    return { auth, request, run: () => handleLocalClaimRequest(auth, request, origin, "trusted-client", "correlation") };
}

describe("local browser claim boundary", () => {
    it("returns private acquisition responses", async () => {
        const { run, auth } = fixture();
        const response = await run();
        expect(response.status).toBe(201);
        expect(response.headers.get("cache-control")).toBe("private, no-store");
        expect((await response.json()).data.ownership).toBe("personal");
        expect(auth.withSessionTransaction).toHaveBeenCalledWith(token, expect.any(Function));
    });

    it.each([
        [{ origin: "https://attacker.test" }, 403],
        [{ authorization: "Bearer unrelated" }, 401],
        [{ cookie: "" }, 401],
        [{ cookie: `__Host-keire-session=${token}; __Host-keire-session=${token}` }, 401],
        [{ "content-type": "text/plain" }, 400],
    ])("rejects invalid request metadata before mutation", async (headers, status) => {
        const { run, auth } = fixture(headers);
        expect((await run()).status).toBe(status);
        expect(auth.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("does not turn missing organization ownership into a personal claim", async () => {
        const { run, auth } = fixture({}, { ...body, ownership: "organization" });
        expect((await run()).status).toBe(400);
        expect(auth.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("bounds streamed content instead of trusting Content-Length", async () => {
        const { run, auth } = fixture({}, { ...body, acceptedLicenseSnapshot: "x".repeat(600000) });
        expect((await run()).status).toBe(400);
        expect(auth.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("does not disclose database failures", async () => {
        const { run, auth } = fixture();
        auth.withSessionTransaction.mockRejectedValue(new Error("private database details"));
        const response = await run();
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("private database details");
    });
});
