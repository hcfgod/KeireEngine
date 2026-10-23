import { mkdtemp, readFile, writeFile, unlink, rmdir } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { describe, expect, it } from "vitest";
import { acquireProductionWriterLease } from "./platform-production-writer.mjs";

describe("production upload single writer", () => {
    it("denies a competing owner and allows clean idempotent release", async () => {
        const directory = await mkdtemp(path.join(os.tmpdir(), "keire-writer-unit-"));
        try {
            const lease = await acquireProductionWriterLease(directory);
            await expect(acquireProductionWriterLease(directory)).rejects.toThrow("already owned");
            expect(lease.release()).toBe(lease.release());
            await lease.release();
            const replacement = await acquireProductionWriterLease(directory);
            await replacement.release();
        } finally {
            await rmdir(directory);
        }
    });

    it("never removes a marker whose ownership changed", async () => {
        const directory = await mkdtemp(path.join(os.tmpdir(), "keire-writer-unit-"));
        const marker = path.join(directory, ".keire-platform-writer.lock");
        try {
            const lease = await acquireProductionWriterLease(directory);
            await writeFile(marker, "changed-owner");
            await expect(lease.release()).rejects.toThrow("changed");
            expect(await readFile(marker, "utf8")).toBe("changed-owner");
        } finally {
            await unlink(marker);
            await rmdir(directory);
        }
    });
});
