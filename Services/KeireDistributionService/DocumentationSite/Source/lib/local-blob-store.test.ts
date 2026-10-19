import { createHash } from "node:crypto";
import { mkdtemp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { LocalBlobStore, type BlobIdentity } from "./local-blob-store";

let directory: string;
let store: LocalBlobStore;
const bytes = Buffer.from("Kéire private asset bytes");
const identity: BlobIdentity = {
    sha256: createHash("sha256").update(bytes).digest("hex"),
    sizeBytes: bytes.length,
};

async function* chunks(value = bytes): AsyncGenerator<Uint8Array> {
    yield value.subarray(0, 5);
    yield value.subarray(5);
}

beforeEach(async () => {
    directory = await mkdtemp(path.join(os.tmpdir(), "keire-blobs-"));
    store = new LocalBlobStore(directory);
});

afterEach(async () => {
    await rm(directory, { recursive: true, force: true });
});

describe("private local blob storage", () => {
    it("publishes verified chunks and permits identical concurrent uploads", async () => {
        await Promise.all([store.put(identity, chunks()), store.put(identity, chunks())]);
        await store.verify(identity);
        expect(await readFile(path.join(directory, identity.sha256))).toEqual(bytes);
        expect(await readdir(directory)).toEqual([identity.sha256]);
    });

    it("reads verified bytes in bounded chunks without reusing yielded buffers", async () => {
        const large = Buffer.alloc(150_000, 42);
        const largeIdentity = { sha256: createHash("sha256").update(large).digest("hex"), sizeBytes: large.length };
        await store.put(largeIdentity, chunks(large));
        const received: Uint8Array[] = [];
        for await (const chunk of store.read(largeIdentity)) {
            expect(chunk.length).toBeLessThanOrEqual(64 * 1024);
            received.push(chunk);
        }
        expect(Buffer.concat(received)).toEqual(large);
    });

    it("rejects corruption before yielding any bytes", async () => {
        const corrupt = Buffer.from(bytes);
        corrupt[corrupt.length - 1] ^= 1;
        await writeFile(path.join(directory, identity.sha256), corrupt);
        const reader = store.read(identity);
        await expect(reader.next()).rejects.toThrow("SHA-256");
    });

    it("closes a cancelled read so the stored file can be removed", async () => {
        await store.put(identity, chunks());
        const reader = store.read(identity);
        expect((await reader.next()).done).toBe(false);
        await reader.return(undefined);
        await rm(path.join(directory, identity.sha256));
        expect(await readdir(directory)).toEqual([]);
    });

    it("supports an empty verified blob", async () => {
        const empty = Buffer.alloc(0);
        const emptyIdentity = { sha256: createHash("sha256").update(empty).digest("hex"), sizeBytes: 0 };
        await store.put(emptyIdentity, chunks(empty));
        expect(await store.read(emptyIdentity).next()).toEqual({ done: true, value: undefined });
    });

    it("rejects traversal identifiers before creating any files", async () => {
        await expect(store.put({ ...identity, sha256: "../outside" }, chunks())).rejects.toThrow("identity");
        expect(await readdir(directory)).toEqual([]);
    });

    it.each([bytes.length - 1, bytes.length + 1])("rejects incorrect declared size %i", async (sizeBytes) => {
        await expect(store.put({ ...identity, sizeBytes }, chunks())).rejects.toThrow();
        expect(await readdir(directory)).toEqual([]);
    });

    it("does not publish a corrupt upload", async () => {
        const corrupt = Buffer.from(bytes);
        corrupt[0] ^= 1;
        await expect(store.put(identity, chunks(corrupt))).rejects.toThrow("SHA-256");
        expect(await readdir(directory)).toEqual([]);
    });

    it("preserves existing corrupt evidence instead of silently replacing it", async () => {
        const corrupt = Buffer.from(bytes);
        corrupt[0] ^= 1;
        await writeFile(path.join(directory, identity.sha256), corrupt);
        await expect(store.put(identity, chunks())).rejects.toThrow("SHA-256");
        expect(await readFile(path.join(directory, identity.sha256))).toEqual(corrupt);
        expect(await readdir(directory)).toEqual([identity.sha256]);
    });

    it("cleans up an interrupted source without publishing partial bytes", async () => {
        async function* interrupted(): AsyncGenerator<Uint8Array> {
            yield bytes.subarray(0, 5);
            throw new Error("source interrupted");
        }
        await expect(store.put(identity, interrupted())).rejects.toThrow("source interrupted");
        expect(await readdir(directory)).toEqual([]);
    });
});
