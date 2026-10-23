import { describe, expect, it, vi } from "vitest";
import { recoverStorageObject } from "./platform-storage-recovery.mjs";

const objectId = "11111111-1111-4111-8111-111111111111";

function fixture() {
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn().mockResolvedValueOnce([[{
            bucket_id: "packages", size_bytes: "3", expected_sha256: "a".repeat(64),
        }]]).mockResolvedValue([{ affectedRows: 1 }]),
    };
    const store = { put: vi.fn(), verify: vi.fn() };
    return { connection, store, pool: { getConnection: vi.fn().mockResolvedValue(connection) },
        stores: new Map([["packages", store]]) };
}

describe("original storage recovery", () => {
    it("marks recovered only after immutable storage and verification succeed", async () => {
        const f = fixture();
        await recoverStorageObject(f.pool, f.stores, objectId, []);
        expect(f.store.put).toHaveBeenCalledWith({ sha256: "a".repeat(64), sizeBytes: 3 }, []);
        expect(f.store.verify.mock.invocationCallOrder[0]).toBeLessThan(f.connection.execute.mock.invocationCallOrder[1]);
        expect(f.connection.commit).toHaveBeenCalledOnce();
    });

    it.each(["put", "verify"])("rolls back metadata when %s fails", async (operation) => {
        const f = fixture();
        f.store[operation].mockRejectedValue(new Error("corrupt bytes"));
        await expect(recoverStorageObject(f.pool, f.stores, objectId, [])).rejects.toThrow("corrupt bytes");
        expect(f.connection.execute).toHaveBeenCalledOnce();
        expect(f.connection.commit).not.toHaveBeenCalled();
        expect(f.connection.rollback).toHaveBeenCalledOnce();
    });

    it("rejects unmapped buckets without storing bytes", async () => {
        const f = fixture();
        await expect(recoverStorageObject(f.pool, new Map(), objectId, [])).rejects.toThrow("explicitly mapped");
        expect(f.store.put).not.toHaveBeenCalled();
    });
});
