import { describe, expect, it, vi } from "vitest";
import { importStorageMetadata, verifyStorageMetadata } from "./platform-storage-import.mjs";

describe("storage metadata verification", () => {
    const source = { id: "object", bucket_id: "private", name: "opaque/path", owner_id: "owner" };
    const plan = {
        buckets: [{ id: "private", public: false }],
        objects: [{ source, sizeBytes: 3, expectedSha256: "a".repeat(64) }],
    };

    function connection(overrides = {}) {
        return { query: vi.fn().mockResolvedValueOnce([[{ id: "private", is_public: 0,
            source_record: { id: "private", public: false } }]]).mockResolvedValueOnce([[{
            id: "object", bucket_id: "private", object_name: "opaque/path", size_bytes: "3",
            expected_sha256: "a".repeat(64), recovered_sha256: null, recovered_at: null,
            source_record: source, ...overrides,
        }]]) };
    }

    it("compares all preserved metadata and explicit unavailable state", async () => {
        expect(await verifyStorageMetadata(connection(), plan)).toEqual({ buckets: 1, objects: 1, recoveredObjects: 0 });
    });

    it.each([
        { object_name: "different" },
        { size_bytes: "4" },
        { recovered_sha256: "a".repeat(64) },
        { source_record: { ...source, owner_id: "changed" } },
    ])("rejects altered or incorrectly recovered records", async (overrides) => {
        await expect(verifyStorageMetadata(connection(overrides), plan)).rejects.toThrow();
    });

    it("rejects an untrusted snapshot before acquiring a connection", async () => {
        const pool = { getConnection: vi.fn() };
        await expect(importStorageMetadata(pool, "{}", "a".repeat(64))).rejects.toThrow();
        expect(pool.getConnection).not.toHaveBeenCalled();
    });
});
