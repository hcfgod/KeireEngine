import { describe, expect, it, vi } from "vitest";
import { createLocalPublicationService } from "./platform-publication-service.mjs";

const siteOrigin = "https://platform.invalid";
const queueSecret = "synthetic-publication-service-credential";

function fixture() {
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn(async (sql) => sql.includes("SELECT key_id") ? [[{ key_id: "test-key" }]] : [[]]),
    };
    const pool = { getConnection: vi.fn(async () => connection), end: vi.fn() };
    const packageStore = { verify: vi.fn(), close: vi.fn() };
    const evidenceStore = { verify: vi.fn(), close: vi.fn() };
    const service = createLocalPublicationService({ pool, packageStore, evidenceStore, siteOrigin, queueSecret });
    return { service, pool, connection, packageStore, evidenceStore };
}

function request(secret = queueSecret) {
    return new Request(`${siteOrigin}/internal/publication/v1/queue/`, {
        method: "POST",
        headers: { "content-type": "application/json", "x-keire-publication-secret": secret },
        body: JSON.stringify({ action: "lease", workerId: "test-worker", signingKeyId: "test-key", leaseSeconds: 300 }),
    });
}

describe("assembled publication service", () => {
    it("calls the real lease transaction through the authenticated boundary", async () => {
        const { service, connection } = fixture();
        try {
            const response = await service.handle(request());
            expect(response.status).toBe(200);
            expect(await response.json()).toEqual({ data: { lease: null } });
            expect(connection.commit).toHaveBeenCalledOnce();
        } finally {
            await service.close();
        }
    });

    it("authenticates before database access", async () => {
        const { service, pool } = fixture();
        expect((await service.handle(request("wrong"))).status).toBe(401);
        expect(await service.handle(new Request(`${siteOrigin}/downloads/`))).toBeNull();
        expect(pool.getConnection).not.toHaveBeenCalled();
        await service.close();
    });

    it("drains accepted work before closing and leaves borrowed dependencies open", async () => {
        const { service, pool, connection, packageStore, evidenceStore } = fixture();
        let release;
        pool.getConnection.mockImplementationOnce(() => new Promise((resolve) => {
            release = () => resolve(connection);
        }));
        const pending = service.handle(request());
        await vi.waitFor(() => expect(release).toBeTypeOf("function"));
        let closed = false;
        const shutdown = service.close().then(() => { closed = true; });
        expect((await service.handle(request())).status).toBe(503);
        expect(closed).toBe(false);
        release();
        expect((await pending).status).toBe(200);
        await shutdown;
        await service.close();
        expect(closed).toBe(true);
        expect(pool.end).not.toHaveBeenCalled();
        expect(packageStore.close).not.toHaveBeenCalled();
        expect(evidenceStore.close).not.toHaveBeenCalled();
    });
});
