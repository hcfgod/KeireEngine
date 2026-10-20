import { describe, expect, it, vi } from "vitest";
import { createValidatorQueueHttp } from "./platform-validator-http.mjs";
import { ValidatorQueueError } from "./platform-validator-error.mjs";

const siteOrigin = "https://platform.invalid";
const endpoint = `${siteOrigin}/internal/validator/v1/queue/`;
const brokerSecret = "synthetic-validator-credential-for-tests";
const uploadId = "11111111-1111-4111-8111-111111111111";
const baseInput = { action: "lease", workerId: "validator-test", leaseSeconds: 900 };

function fixture() {
    const service = {
        lease: vi.fn(async () => null),
        renew: vi.fn(async () => ({ leaseExpiresAt: "2030-01-01T00:00:00Z" })),
        complete: vi.fn(async () => ({ reportId: "report-fixture" })),
    };
    return { service, boundary: createValidatorQueueHttp({ service, brokerSecret, siteOrigin }) };
}

function request(body = baseInput, headers = {}, url = endpoint) {
    return new Request(url, {
        method: "POST",
        headers: { "content-type": "application/json", "x-keire-validator-secret": brokerSecret, ...headers },
        body: JSON.stringify(body),
    });
}

describe("dedicated validator HTTP boundary", () => {
    it("returns the broker's existing empty queue contract", async () => {
        const { boundary, service } = fixture();
        const response = await boundary.handle(request());
        expect(response.status).toBe(200);
        expect(response.headers.get("cache-control")).toBe("private, no-store");
        expect(await response.json()).toEqual({ data: { lease: null } });
        expect(service.lease).toHaveBeenCalledWith("validator-test", 900);
    });

    it.each(["origin", "cookie", "authorization", "apikey"])("rejects %s credentials even with the broker secret", async (name) => {
        const { boundary, service } = fixture();
        const incoming = request(baseInput, { [name]: "not-accepted" });
        expect((await boundary.handle(incoming)).status).toBe(403);
        expect(incoming.bodyUsed).toBe(false);
        expect(service.lease).not.toHaveBeenCalled();
    });

    it.each(["", "short", "wrong-credential-that-is-long-enough", "x".repeat(257)])(
        "rejects invalid broker credentials without parsing private bodies", async (secret) => {
            const { boundary, service } = fixture();
            const incoming = request(baseInput, { "x-keire-validator-secret": secret });
            expect((await boundary.handle(incoming)).status).toBe(401);
            expect(incoming.bodyUsed).toBe(false);
            expect(service.lease).not.toHaveBeenCalled();
        });

    it.each([
        { ...baseInput, action: "unknown" }, { ...baseInput, workerId: "../worker" },
        { ...baseInput, leaseSeconds: 59 }, { ...baseInput, leaseSeconds: 1801 },
        { ...baseInput, action: "renew", uploadId: "bad" },
        { ...baseInput, action: "complete", uploadId, report: null },
        { ...baseInput, action: "complete", uploadId, report: [] },
    ])("rejects malformed operations before invoking the service", async (body) => {
        const { boundary, service } = fixture();
        expect((await boundary.handle(request(body))).status).toBe(400);
        expect(service.lease).not.toHaveBeenCalled();
        expect(service.renew).not.toHaveBeenCalled();
        expect(service.complete).not.toHaveBeenCalled();
    });

    it("bounds the complete streamed body even without Content-Length", async () => {
        const { boundary, service } = fixture();
        const incoming = request({ ...baseInput, padding: "x".repeat(1024 * 1024) });
        expect(incoming.headers.has("content-length")).toBe(false);
        expect((await boundary.handle(incoming)).status).toBe(400);
        expect(service.lease).not.toHaveBeenCalled();
    });

    it("routes renewal and returns a lost lease as a conflict", async () => {
        const { boundary, service } = fixture();
        service.renew.mockRejectedValue(new ValidatorQueueError(409, "validator.lease_lost", "The validation lease was lost."));
        const response = await boundary.handle(request({ ...baseInput, action: "renew", uploadId }));
        expect(response.status).toBe(409);
        expect(service.renew).toHaveBeenCalledWith(uploadId, "validator-test", 900);
    });

    it("sends reports only to the service's mandatory verification operation", async () => {
        const { boundary, service } = fixture();
        const report = { schemaVersion: 1, passed: false };
        expect((await boundary.handle(request({ ...baseInput, action: "complete", uploadId, report }))).status).toBe(200);
        expect(service.complete).toHaveBeenCalledWith(uploadId, "validator-test", report);
    });

    it("hides infrastructure errors and credential material", async () => {
        const { boundary, service } = fixture();
        service.lease.mockRejectedValue(new Error(`SQL and private credential: ${brokerSecret}`));
        const response = await boundary.handle(request());
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain(brokerSecret);
    });

    it("rejects insecure origins, query credentials, and non-POST methods", async () => {
        const { boundary, service } = fixture();
        expect((await boundary.handle(request(baseInput, {}, endpoint.replace("https:", "http:")))).status).toBe(403);
        expect((await boundary.handle(request(baseInput, {}, `${endpoint}?secret=value`))).status).toBe(403);
        expect((await boundary.handle(new Request(endpoint))).status).toBe(405);
        expect(await boundary.handle(new Request(`${siteOrigin}/other/`))).toBeNull();
        expect(service.lease).not.toHaveBeenCalled();
    });

    it("fails closed after shutdown and requires complete configuration", async () => {
        const { boundary, service } = fixture();
        boundary.close();
        boundary.close();
        expect((await boundary.handle(request())).status).toBe(503);
        expect(service.lease).not.toHaveBeenCalled();
        expect(() => createValidatorQueueHttp({ service: {}, brokerSecret, siteOrigin })).toThrow("incomplete");
        expect(() => createValidatorQueueHttp({ service, brokerSecret: "", siteOrigin })).toThrow("credential");
        expect(() => createValidatorQueueHttp({ service, brokerSecret, siteOrigin: "http://platform.invalid" }))
            .toThrow("HTTPS");
    });
});
