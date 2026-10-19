import { describe, expect, it, vi } from "vitest";
import { PlatformBackend, selectPlatformBackend } from "./platform-backend.mjs";

function deferred() {
    let resolve!: () => void;
    const promise = new Promise<void>((complete) => { resolve = complete; });
    return { promise, resolve };
}

function fixture() {
    const key = Buffer.alloc(32, 42);
    const pool = { query: vi.fn().mockResolvedValue([[]]), end: vi.fn().mockResolvedValue(undefined) };
    const authentication = { close: vi.fn() };
    const loadLocalSecrets = vi.fn().mockResolvedValue({ settings: {}, accountEncryptionKey: key });
    const createPool = vi.fn().mockResolvedValue(pool);
    const createAuthentication = vi.fn().mockResolvedValue(authentication);
    const backend = new PlatformBackend({ provider: "mysql", loadLocalSecrets, createPool, createAuthentication });
    return { backend, pool, authentication, key, loadLocalSecrets, createPool, createAuthentication };
}

describe("application-owned backend lifecycle", () => {
    it("owns one optional mailer and drains requests before closing it", async () => {
        const data = fixture();
        const mailer = { sendVerification: vi.fn(), close: vi.fn() };
        const createMailer = vi.fn().mockResolvedValue(mailer);
        const backend = new PlatformBackend({ provider: "mysql", loadLocalSecrets: data.loadLocalSecrets,
            createPool: data.createPool, createAuthentication: data.createAuthentication, createMailer });
        const gate = deferred();
        const started = deferred();
        const request = backend.withBackend(async (services) => {
            expect(services.emailAuthentication).not.toBeNull();
            started.resolve();
            await gate.promise;
        });
        await started.promise;
        const closing = backend.close();
        expect(mailer.close).not.toHaveBeenCalled();
        gate.resolve();
        await request;
        await closing;
        await backend.close();
        expect(createMailer).toHaveBeenCalledTimes(1);
        expect(mailer.close).toHaveBeenCalledTimes(1);
        expect(data.authentication.close).toHaveBeenCalledTimes(1);
        expect(data.pool.end).toHaveBeenCalledTimes(1);
    });

    it("cleans up authentication and database after mailer initialization fails", async () => {
        const data = fixture();
        const backend = new PlatformBackend({ provider: "mysql", loadLocalSecrets: data.loadLocalSecrets,
            createPool: data.createPool, createAuthentication: data.createAuthentication,
            createMailer: async () => { throw new Error("unavailable"); } });
        await expect(backend.withBackend(() => undefined)).rejects.toThrow("unavailable");
        await backend.close();
        expect(data.key.equals(Buffer.alloc(32))).toBe(true);
        expect(data.authentication.close).toHaveBeenCalledTimes(1);
        expect(data.pool.end).toHaveBeenCalledTimes(1);
    });

    it("defaults to Supabase and rejects invalid explicit selectors", () => {
        expect(selectPlatformBackend(undefined)).toBe("supabase");
        expect(selectPlatformBackend("mysql")).toBe("mysql");
        expect(() => selectPlatformBackend("MYSQL")).toThrow();
        expect(() => selectPlatformBackend("")).toThrow();
    });

    it("does not load MySQL secrets when Supabase is selected", async () => {
        const loader = vi.fn();
        const backend = new PlatformBackend({ provider: "supabase", loadLocalSecrets: loader });
        expect(await backend.withBackend((value) => value)).toEqual({ provider: "supabase" });
        await backend.close();
        expect(loader).not.toHaveBeenCalled();
    });

    it("shares one initialization across concurrent requests and erases the transferred key", async () => {
        const fixtureData = fixture();
        const results = await Promise.all(Array.from({ length: 12 }, () =>
            fixtureData.backend.withBackend((value) => value)));
        expect(results.every((value) => value === results[0])).toBe(true);
        expect(fixtureData.createPool).toHaveBeenCalledTimes(1);
        expect(fixtureData.createAuthentication).toHaveBeenCalledTimes(1);
        expect(fixtureData.key.equals(Buffer.alloc(32))).toBe(true);
        await fixtureData.backend.close();
        expect(fixtureData.pool.end).toHaveBeenCalledTimes(1);
        expect(fixtureData.authentication.close).toHaveBeenCalledTimes(1);
    });

    it("drains active requests before closing and rejects new requests", async () => {
        const { backend, pool, authentication } = fixture();
        const started = deferred();
        const finish = deferred();
        const request = backend.withBackend(async () => {
            started.resolve();
            await finish.promise;
        });
        await started.promise;
        const shutdown = backend.close();
        expect(backend.close()).toBe(shutdown);
        expect(authentication.close).not.toHaveBeenCalled();
        await expect(backend.withBackend(() => undefined)).rejects.toThrow("shutting down");
        finish.resolve();
        await Promise.all([request, shutdown]);
        expect(pool.end).toHaveBeenCalledTimes(1);
    });

    it("retains initialization failures and disposes partial resources without provider fallback", async () => {
        const { backend, pool, key, loadLocalSecrets } = fixture();
        pool.query.mockRejectedValue(new Error("connection unavailable"));
        await expect(backend.withBackend(() => undefined)).rejects.toThrow("connection unavailable");
        await expect(backend.withBackend(() => undefined)).rejects.toThrow("connection unavailable");
        expect(loadLocalSecrets).toHaveBeenCalledTimes(1);
        expect(pool.end).toHaveBeenCalledTimes(1);
        expect(key.equals(Buffer.alloc(32))).toBe(true);
        await backend.close();
        expect(pool.end).toHaveBeenCalledTimes(1);
    });

    it("can close before first use without allocating resources", async () => {
        const { backend, loadLocalSecrets } = fixture();
        await backend.close();
        expect(loadLocalSecrets).not.toHaveBeenCalled();
        await expect(backend.withBackend(() => undefined)).rejects.toThrow("shutting down");
    });

    it("waits for initialization already in flight before disposing resources", async () => {
        const { backend, pool, loadLocalSecrets, key } = fixture();
        const loading = deferred();
        loadLocalSecrets.mockImplementation(async () => {
            await loading.promise;
            return { settings: {}, accountEncryptionKey: key };
        });
        const request = backend.withBackend(() => "finished");
        const shutdown = backend.close();
        expect(pool.end).not.toHaveBeenCalled();
        loading.resolve();
        expect(await request).toBe("finished");
        await shutdown;
        expect(pool.end).toHaveBeenCalledTimes(1);
    });

    it("releases the request lease after a handler error", async () => {
        const { backend, pool } = fixture();
        await expect(backend.withBackend(() => { throw new Error("handler"); })).rejects.toThrow("handler");
        await backend.close();
        expect(pool.end).toHaveBeenCalledTimes(1);
    });
});
