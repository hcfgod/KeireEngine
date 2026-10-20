import { afterEach, describe, expect, it } from "vitest";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { PrivateUploadFileStore } from "./platform-upload-file-store.mjs";

const roots: string[] = [];

afterEach(async () => {
    for (const root of roots.splice(0)) {
        if (path.dirname(root) !== path.resolve(tmpdir()) || !path.basename(root).startsWith("keire-spool-")) {
            throw new Error("Unexpected test directory.");
        }
        await rm(root, { recursive: true, force: true });
    }
});

describe("private resumable file storage", () => {
    it("preserves existing bytes and metadata on duplicate creation and reopens after restart", async () => {
        const root = await mkdtemp(path.join(tmpdir(), "keire-spool-"));
        roots.push(root);
        const id = "a".repeat(32);
        const store = new PrivateUploadFileStore({ directory: root });
        await store.create({ id, size: 6, offset: 0, metadata: { reservationId: "original" } });
        await writeFile(path.join(root, id), "abc");
        await expect(store.create({ id, size: 99, metadata: { reservationId: "replacement" } }))
            .rejects.toMatchObject({ code: "EEXIST" });
        expect(await readFile(path.join(root, id), "utf8")).toBe("abc");
        const restarted = new PrivateUploadFileStore({ directory: root });
        const upload = await restarted.getUpload(id);
        expect(upload.offset).toBe(3);
        expect(upload.size).toBe(6);
        expect(upload.metadata.reservationId).toBe("original");
        expect(() => restarted.resolve("../outside")).toThrow("identifier");
        expect(() => restarted.resolve(`${id}/nested`)).toThrow("identifier");
    });

    it("requires an existing absolute directory rather than creating public storage", () => {
        expect(() => new PrivateUploadFileStore({ directory: "relative" })).toThrow("absolute");
        expect(() => new PrivateUploadFileStore({ directory: path.join(tmpdir(), `absent-${Date.now()}`) })).toThrow();
    });
});
