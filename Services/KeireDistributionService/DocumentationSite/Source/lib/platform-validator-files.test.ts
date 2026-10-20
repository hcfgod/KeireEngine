import { createHash, randomBytes } from "node:crypto";
import { describe, expect, it, vi } from "vitest";
import { createValidatorCapabilities } from "./platform-validator-capabilities.mjs";
import { createValidatorFileHttp } from "./platform-validator-files.mjs";

function fixture() {
    const content = Buffer.from("private-package");
    const upload = { id: "11111111-1111-4111-8111-111111111111",
        expected_sha256: createHash("sha256").update(content).digest("hex"), expected_size_bytes: content.length };
    const state = { active: true };
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn(async () => [state.active ? [upload] : []]),
    };
    const pool = { getConnection: vi.fn(async () => connection) };
    const capabilities = createValidatorCapabilities(randomBytes(32), "https://platform.invalid");
    const address = (purpose) => capabilities.issue({ uploadId: upload.id, workerId: "worker-1", attempt: 1, purpose });
    const packageStore = { read: vi.fn(async function* () { yield content; }) };
    const received: Buffer[] = [];
    const evidenceStore = { put: vi.fn(async (_identity, chunks) => {
        for await (const chunk of chunks) received.push(Buffer.from(chunk));
    }) };
    const handle = createValidatorFileHttp({ capabilities, pool, packageStore, evidenceStore });
    return { handle, address, content, state, pool, packageStore, evidenceStore, received, capabilities };
}

describe("private validator file transport", () => {
    it("streams only the authorized package with no-store headers", async () => {
        const { handle, address, content, packageStore, capabilities } = fixture();
        const response = await handle(new Request(address("package")));
        expect(response.status).toBe(200);
        expect(response.headers.get("cache-control")).toBe("private, no-store");
        expect(response.headers.get("content-length")).toBe(String(content.length));
        expect(Buffer.from(await response.arrayBuffer())).toEqual(content);
        expect(packageStore.read).toHaveBeenCalledOnce();
        capabilities.close();
    });

    it("hashes bounded evidence and stores its exact bytes", async () => {
        const { handle, address, evidenceStore, received, capabilities } = fixture();
        const body = '{"schemaVersion":1}';
        const response = await handle(new Request(address("evidence"), {
            method: "PUT", headers: { "content-type": "application/json", "x-upsert": "false" }, body,
        }));
        expect(response.status).toBe(200);
        expect(Buffer.concat(received).toString()).toBe(body);
        expect(evidenceStore.put.mock.calls[0][0]).toEqual({
            sha256: createHash("sha256").update(body).digest("hex"), sizeBytes: Buffer.byteLength(body),
        });
        capabilities.close();
    });

    it("rejects expired leases before reading storage", async () => {
        const { handle, address, state, packageStore, capabilities } = fixture();
        state.active = false;
        expect((await handle(new Request(address("package")))).status).toBe(403);
        expect(packageStore.read).not.toHaveBeenCalled();
        capabilities.close();
    });

    it("does not accept evidence after the lease is revoked during storage", async () => {
        const { handle, address, state, evidenceStore, capabilities } = fixture();
        evidenceStore.put.mockImplementation(async () => { state.active = false; });
        expect((await handle(new Request(address("evidence"), {
            method: "PUT", headers: { "content-type": "application/json" }, body: "{}",
        }))).status).toBe(403);
        capabilities.close();
    });

    it("rejects oversized evidence without storing it", async () => {
        const { handle, address, evidenceStore, capabilities } = fixture();
        expect((await handle(new Request(address("evidence"), {
            method: "PUT", headers: { "content-type": "application/json" }, body: "x".repeat(8 * 1024 * 1024 + 1),
        }))).status).toBe(413);
        expect(evidenceStore.put).not.toHaveBeenCalled();
        capabilities.close();
    });

    it.each(["origin", "cookie", "authorization", "apikey"])("rejects browser credentials %s before DB access", async (header) => {
        const { handle, address, pool, capabilities } = fixture();
        expect((await handle(new Request(address("package"), { headers: { [header]: "not-allowed" } }))).status).toBe(403);
        expect(pool.getConnection).not.toHaveBeenCalled();
        capabilities.close();
    });

    it("returns a safe failure before responding with package bytes if storage is unavailable", async () => {
        const { handle, address, packageStore, capabilities } = fixture();
        packageStore.read.mockImplementation(async function* () { throw new Error("private-storage-path"); });
        const response = await handle(new Request(address("package")));
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("private-storage-path");
        capabilities.close();
    });
});
