import { describe, expect, it, vi } from "vitest";
import { createPublicationQueueHttp } from "./platform-publication-http.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

const siteOrigin = "https://platform.invalid";
const endpoint = `${siteOrigin}/internal/publication/v1/queue/`;
const queueSecret = "synthetic-publication-credential-for-tests";
const jobId = "11111111-1111-4111-8111-111111111111";
const input = { action: "lease", workerId: "signer-test", leaseSeconds: 300, signingKeyId: "test-key" };

function fixture() {
    const service = {
        lease: vi.fn(async () => null),
        renew: vi.fn(async () => ({ leaseExpiresAt: "2030-01-01T00:00:00Z" })),
        complete: vi.fn(async () => ({ publication_id: "published" })),
        fail: vi.fn(async () => ({ state: "retry_wait" })),
    };
    return { service, boundary: createPublicationQueueHttp({ service, queueSecret, siteOrigin }) };
}

function request(body = input, headers = {}, url = endpoint) {
    return new Request(url, {
        method: "POST",
        headers: { "content-type": "application/json", "x-keire-publication-secret": queueSecret, ...headers },
        body: JSON.stringify(body),
    });
}

describe("publication signer HTTP boundary", () => {
    it("preserves the empty queue contract and passes signing key identity", async () => {
        const { boundary, service } = fixture();
        const response = await boundary.handle(request());
        expect(response.status).toBe(200);
        expect(await response.json()).toEqual({ data: { lease: null } });
        expect(service.lease).toHaveBeenCalledWith("signer-test", "test-key", 300);
        expect(response.headers.get("cache-control")).toBe("private, no-store");
    });

    it("writes an exact maximum int64 JSON number, with numeric sizes and escaped strings", async () => {
        const { boundary, service } = fixture();
        service.lease.mockResolvedValue({
            publication_sequence: "9223372036854775807", artifact_size_bytes: "123", evidence_size_bytes: "2",
            validation_attestation: '{"sequence":"fake"}',
        });
        const text = await (await boundary.handle(request())).text();
        expect(text).toContain('"publication_sequence":9223372036854775807');
        expect(text).toContain('"artifact_size_bytes":123');
        expect(JSON.parse(text).data.lease.validation_attestation).toBe('{"sequence":"fake"}');
    });

    it.each(["9223372036854775808", "01", "1e2", -1, Number.MAX_SAFE_INTEGER + 1])(
        "refuses an invalid internal sequence %s", async (sequence) => {
            const { boundary, service } = fixture();
            service.lease.mockResolvedValue({
                publication_sequence: sequence, artifact_size_bytes: "1", evidence_size_bytes: "2",
            });
            expect((await boundary.handle(request())).status).toBe(503);
        },
    );

    it.each(["origin", "cookie", "authorization", "apikey"])("rejects browser/API credentials: %s", async (name) => {
        const { boundary, service } = fixture();
        const incoming = request(input, { [name]: "rejected" });
        expect((await boundary.handle(incoming)).status).toBe(403);
        expect(incoming.bodyUsed).toBe(false);
        expect(service.lease).not.toHaveBeenCalled();
    });

    it.each(["", "short", "incorrect-credential-that-is-long-enough"])("rejects wrong credentials: %s", async (secret) => {
        const { boundary } = fixture();
        const incoming = request(input, { "x-keire-publication-secret": secret });
        expect((await boundary.handle(incoming)).status).toBe(401);
        expect(incoming.bodyUsed).toBe(false);
    });

    it.each([
        { ...input, signingKeyId: "../key" }, { ...input, leaseSeconds: 901 },
        { ...input, action: "renew", jobId: "bad" },
        { ...input, action: "complete", jobId, signedManifest: "x".repeat(65537) },
        { ...input, action: "fail", jobId, errorCode: "publication.failed", retryable: "true" },
    ])("rejects malformed operation input", async (body) => {
        const { boundary, service } = fixture();
        expect((await boundary.handle(request(body))).status).toBe(400);
        for (const operation of Object.values(service)) {
            expect(operation).not.toHaveBeenCalled();
        }
    });

    it("routes renew, completion, and failure using the C# request names", async () => {
        const { boundary, service } = fixture();
        expect((await boundary.handle(request({ ...input, action: "renew", jobId }))).status).toBe(200);
        expect(service.renew).toHaveBeenCalledWith(jobId, "signer-test", 300);
        await boundary.handle(request({ ...input, action: "complete", jobId, signedManifest: "signed-text" }));
        expect(service.complete).toHaveBeenCalledWith(jobId, "signer-test", "signed-text");
        await boundary.handle(request({ ...input, action: "fail", jobId, errorCode: "publication.failed", retryable: true }));
        expect(service.fail).toHaveBeenCalledWith(jobId, "signer-test", "publication.failed", true);
    });

    it("returns domain errors but suppresses internal diagnostics", async () => {
        const { boundary, service } = fixture();
        service.lease.mockRejectedValue(new LocalUploadError(409, "publication.conflict", "Lease changed."));
        expect((await boundary.handle(request())).status).toBe(409);
        service.lease.mockRejectedValue(new Error("private database diagnostic"));
        const response = await boundary.handle(request());
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("private database");
    });

    it("rejects another origin, query parameters, and operations after shutdown", async () => {
        const { boundary } = fixture();
        expect((await boundary.handle(request(input, {}, endpoint.replace("platform.invalid", "other.invalid")))).status).toBe(403);
        expect((await boundary.handle(request(input, {}, `${endpoint}?secret=value`))).status).toBe(403);
        expect(await boundary.handle(request(input, {}, `${siteOrigin}/other/`))).toBeNull();
        expect((await boundary.handle(new Request(endpoint))).status).toBe(405);
        boundary.close();
        expect((await boundary.handle(request())).status).toBe(503);
    });
});
