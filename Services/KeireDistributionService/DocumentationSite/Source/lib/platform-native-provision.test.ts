import { describe, expect, it, vi } from "vitest";
import { createNativeSchema } from "./platform-native-provision.mjs";

describe("initial native authentication schema", () => {
    it("creates all four tables only after a read-only preflight", async () => {
        const execute = vi.fn().mockResolvedValue([[]]);
        const names = await createNativeSchema({ execute });
        expect(names).toHaveLength(4);
        expect(execute).toHaveBeenCalledTimes(5);
        expect(execute.mock.calls[0][1]).toEqual(names);
        for (const [sql] of execute.mock.calls.slice(1)) {
            expect(sql).toMatch(/^CREATE TABLE platform_native_/);
            expect(sql).not.toContain("IF NOT EXISTS");
        }
    });

    it("refuses existing or partial schemas without mutation", async () => {
        const execute = vi.fn().mockResolvedValue([[{ TABLE_NAME: "platform_native_sessions" }]]);
        await expect(createNativeSchema({ execute })).rejects.toThrow("already exist");
        expect(execute).toHaveBeenCalledTimes(1);
    });

    it("stops on DDL failure without deleting committed tables", async () => {
        const failure = new Error("DDL failed");
        const execute = vi.fn().mockResolvedValueOnce([[]]).mockResolvedValueOnce([]).mockRejectedValueOnce(failure);
        await expect(createNativeSchema({ execute })).rejects.toBe(failure);
        expect(execute).toHaveBeenCalledTimes(3);
    });
});
