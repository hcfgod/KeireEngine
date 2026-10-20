import { randomBytes } from "node:crypto";
import { describe, expect, it, vi } from "vitest";
import { createLocalValidatorService } from "./platform-validator-service.mjs";

const siteOrigin = "https://platform.invalid";
const brokerSecret = "isolated-test-broker-credential-123456789";
const upload = {
    id: "11111111-1111-4111-8111-111111111111",
    version_id: "22222222-2222-4222-8222-222222222222",
    storage_bucket: "marketplace-quarantine",
    storage_path: "isolated/package",
    expected_sha256: "a".repeat(64),
    expected_size_bytes: 7,
};

function fixture() {
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn(async (sql) => {
            if (sql.includes("ORDER BY upload.lease_expires_at")) {
                return [[]];
            }
            if (sql.includes("ORDER BY upload.uploaded_at")) {
                return [[upload]];
            }
            if (sql.includes("DATE_FORMAT")) {
                return [[{ expires_at: "2030-01-01T00:15:00.000000Z", validation_attempts: 2 }]];
            }
            return [[upload]];
        }),
    };
    const pool = { getConnection: vi.fn(async () => connection) };
    const packageStore = { read: vi.fn(async function* () {
        yield Buffer.from("package");
    }) };
    const evidenceStore = { put: vi.fn(), verify: vi.fn() };
    const service = createLocalValidatorService({
        pool, packageStore, evidenceStore, siteOrigin, brokerSecret, transferKey: randomBytes(32),
    });
    return { service, pool, connection, packageStore };
}

function leaseRequest(secret = brokerSecret) {
    return new Request(`${siteOrigin}/internal/validator/v1/queue/`, {
        method: "POST",
        headers: { "content-type": "application/json", "x-keire-validator-secret": secret },
        body: JSON.stringify({ action: "lease", workerId: "worker-1", leaseSeconds: 900 }),
    });
}

describe("assembled local validator service", () => {
    it("returns the strict broker contract and usable private package link", async () => {
        const { service, connection } = fixture();
        try {
            const response = await service.handle(leaseRequest());
            expect(response.status).toBe(200);
            const { data: { lease } } = await response.json();
            expect(Object.keys(lease).sort()).toEqual([
                "uploadId", "versionId", "storageBucket", "storagePath", "evidenceStoragePath",
                "expectedSizeBytes", "expectedSha256", "leaseExpiresAt", "downloadUrl", "evidenceUploadUrl",
            ].sort());
            const packageResponse = await service.handle(new Request(lease.downloadUrl));
            expect(packageResponse.status).toBe(200);
            expect(await packageResponse.text()).toBe("package");
            const authorization = connection.execute.mock.calls.find(([sql]) => sql.includes("FOR SHARE"));
            expect(authorization?.[1]).toEqual([upload.id, "worker-1", 2]);
        } finally {
            await service.close();
        }
    });

    it("authenticates before touching the database and leaves unrelated routing alone", async () => {
        const { service, pool } = fixture();
        try {
            expect((await service.handle(leaseRequest("wrong"))).status).toBe(401);
            expect(await service.handle(new Request(`${siteOrigin}/downloads/`))).toBeNull();
            expect(pool.getConnection).not.toHaveBeenCalled();
        } finally {
            await service.close();
        }
    });

    it("drains an accepted lease before clearing keys and rejects new requests", async () => {
        const { service, pool, connection } = fixture();
        let releaseConnection;
        pool.getConnection.mockImplementationOnce(() => new Promise((resolve) => {
            releaseConnection = () => resolve(connection);
        }));
        const pending = service.handle(leaseRequest());
        await vi.waitFor(() => expect(releaseConnection).toBeTypeOf("function"));
        let closed = false;
        const shutdown = service.close().then(() => { closed = true; });
        expect((await service.handle(leaseRequest())).status).toBe(503);
        expect(closed).toBe(false);
        releaseConnection();
        expect((await pending).status).toBe(200);
        await shutdown;
        expect(closed).toBe(true);
        await service.close();
    });
});
