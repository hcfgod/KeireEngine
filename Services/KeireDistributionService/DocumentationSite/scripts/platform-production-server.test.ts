import http from "node:http";
import os from "node:os";
import { describe, expect, it, vi } from "vitest";
import { startProductionServer } from "./platform-production-server.mjs";

function request(port, target, headers = {}, method = "GET") {
    return new Promise((resolve, reject) => {
        const outgoing = http.request({ hostname: "127.0.0.1", port, path: target, headers, method }, (response) => {
            const chunks = [];
            response.on("data", (chunk) => chunks.push(chunk));
            response.on("end", () => resolve({ status: response.statusCode, body: Buffer.concat(chunks).toString("utf8") }));
        });
        outgoing.on("error", reject);
        outgoing.end();
    });
}
const headers = { host: "platform.invalid", "x-forwarded-host": "platform.invalid", "x-forwarded-proto": "https",
    "x-keire-client-ip": "203.0.113.10" };
async function fixture(handler) {
    const pool = { query: vi.fn(async () => [[]]) };
    const backend = { withBackend: vi.fn(async (action) => action({ pool })), close: vi.fn(async () => {}) };
    const entry = { options: { mode: "middleware" }, handler: handler ?? (async (req, res, next, locals) => {
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ identity: locals.platformClientIdentity,
            host: req.headers.host, protocol: req.headers["x-forwarded-proto"], forwarded: req.headers["x-forwarded-for"] }));
    }) };
    const host = await startProductionServer({ backend, entry,
        configuration: { siteOrigin: "https://platform.invalid", port: 0, webRoot: os.tmpdir() } });
    return { backend, pool, host, port: host.address.port };
}

describe("production loopback listener", () => {
    it("accepts verified proxy metadata and rejects direct application requests", async () => {
        const f = await fixture();
        try {
            expect(f.host.address.address).toBe("127.0.0.1");
            const response = await request(f.port, "/auth/v1/user", { ...headers, "x-forwarded-for": "attacker" });
            expect(response.status).toBe(200);
            expect(JSON.parse(response.body)).toEqual({ identity: "ip:203.0.113.10", host: "platform.invalid", protocol: "https" });
            expect((await request(f.port, "/auth/v1/user")).status).toBe(400);
            expect((await request(f.port, "/auth/v1/user", { ...headers, "x-forwarded-proto": "http" })).status).toBe(400);
        } finally {
            await f.host.close();
        }
        expect(f.backend.close).toHaveBeenCalledOnce();
    });

    it("health checks database readiness without exposing diagnostics or requiring proxy credentials", async () => {
        const f = await fixture();
        try {
            expect((await request(f.port, "/health/", {}, "HEAD")).status).toBe(204);
            expect(f.pool.query).toHaveBeenCalledWith("SELECT 1");
            f.pool.query.mockRejectedValue(new Error("database-password-secret"));
            const failed = await request(f.port, "/health/");
            expect(failed.status).toBe(503);
            expect(failed.body).not.toContain("secret");
        } finally {
            await f.host.close();
        }
    });

    it("drains an accepted response before closing backend services", async () => {
        let release;
        let entered;
        const started = new Promise((resolve) => { entered = resolve; });
        const f = await fixture(async (req, res) => {
            entered();
            await new Promise((resolve) => { release = resolve; });
            res.end("finished");
        });
        const pending = request(f.port, "/auth/v1/user", headers);
        await started;
        const closing = f.host.close();
        expect(f.host.close()).toBe(closing);
        expect(f.backend.close).not.toHaveBeenCalled();
        release();
        expect((await pending).body).toBe("finished");
        await closing;
        expect(f.backend.close).toHaveBeenCalledOnce();
    });

    it("releases owned services when the configured port is already occupied", async () => {
        const existing = await fixture();
        const backend = { withBackend: vi.fn(), close: vi.fn(async () => {}) };
        try {
            await expect(startProductionServer({ backend,
                entry: { options: { mode: "middleware" }, handler: async () => {} },
                configuration: { siteOrigin: "https://platform.invalid", port: existing.port, webRoot: os.tmpdir() } }))
                .rejects.toThrow("listener could not start");
            expect(backend.close).toHaveBeenCalledOnce();
        } finally {
            await existing.host.close();
        }
    });

    it("rejects standalone builds before opening a listener", async () => {
        const backend = { close: vi.fn() };
        await expect(startProductionServer({ backend, entry: { options: { mode: "standalone" } },
            configuration: { siteOrigin: "https://platform.invalid", port: 0 } })).rejects.toThrow("middleware");
    });
});
