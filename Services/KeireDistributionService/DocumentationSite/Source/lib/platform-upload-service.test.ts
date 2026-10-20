import { afterEach, describe, expect, it, vi } from "vitest";
import { mkdir, mkdtemp, rm } from "node:fs/promises";
import path from "node:path";
import { tmpdir } from "node:os";
import { createLocalUploadService } from "./platform-upload-service.mjs";

const roots: string[] = [];

afterEach(async () => {
    for (const root of roots.splice(0)) {
        if (path.dirname(root) !== path.resolve(tmpdir()) || !path.basename(root).startsWith("keire-service-")) {
            throw new Error("Unexpected fixture directory.");
        }
        await rm(root, { recursive: true, force: true });
    }
});

describe("owned upload service", () => {
    it("verifies separate directories and drains requests before closing", async () => {
        const root = await mkdtemp(path.join(tmpdir(), "keire-service-"));
        roots.push(root);
        const spoolDirectory = path.join(root, "spool");
        const blobDirectory = path.join(root, "blobs");
        await mkdir(spoolDirectory, { mode: 0o700 });
        await mkdir(blobDirectory, { mode: 0o700 });
        const verifyDirectory = vi.fn(async (directory) => directory);
        let finish;
        let entered;
        const started = new Promise<void>((resolve) => { entered = resolve; });
        const gate = new Promise<void>((resolve) => { finish = resolve; });
        const authentication = { withSessionTransaction: async () => {
            entered();
            await gate;
            return null;
        } };
        const service = await createLocalUploadService({
            authentication, spoolDirectory, blobDirectory, verifyDirectory, siteOrigin: "https://qa.invalid.example",
        });
        expect(verifyDirectory.mock.calls.map(([directory]) => directory)).toEqual([spoolDirectory, blobDirectory]);
        const request = new Request(`https://qa.invalid.example/publisher/v1/transfer/${"1".repeat(32)}`, {
            method: "HEAD", headers: { "tus-resumable": "1.0.0",
                cookie: `__Host-keire-session=${"a".repeat(43)}`,
                "x-signature": "11111111-1111-4111-8111-111111111111" },
        });
        const pending = service.handle(request);
        await started;
        let closed = false;
        const closing = service.close().then(() => { closed = true; });
        await Promise.resolve();
        expect(closed).toBe(false);
        await expect(service.handle(request)).rejects.toThrow("shutting down");
        finish();
        expect((await pending).status).toBe(401);
        await closing;
        expect(closed).toBe(true);
        await service.close();
    });

    it("rejects overlapping storage roots", async () => {
        await expect(createLocalUploadService({
            authentication: {}, spoolDirectory: path.resolve("fixture"), blobDirectory: path.resolve("fixture", "child"),
            verifyDirectory: async (value) => value, siteOrigin: "https://qa.invalid.example",
        })).rejects.toThrow("separate directories");
    });
});
