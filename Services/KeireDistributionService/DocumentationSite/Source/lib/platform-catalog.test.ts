import { describe, expect, it, vi } from "vitest";
import { decodeCatalogCursor, encodeCatalogCursor } from "./api";
import { localCatalogPage, localMarketplaceListing } from "./platform-catalog.mjs";

describe("local catalog boundaries", () => {
    it("renders a disabled listing without exposing categories", async () => {
        const pool = { execute: vi.fn().mockResolvedValue([[{ enabled: 0 }]]) };
        expect(await localMarketplaceListing(pool)).toEqual({ status: "disabled", products: [], categories: [] });
        expect(pool.execute).toHaveBeenCalledTimes(1);
    });

    it("turns database failures into an unavailable listing without leaking diagnostics", async () => {
        const pool = { execute: vi.fn().mockRejectedValue(new Error("private driver details")) };
        expect(await localMarketplaceListing(pool)).toEqual({ status: "unavailable", products: [], categories: [] });
    });
    it("preserves microseconds in pagination cursors", () => {
        const cursor = { featured: true, publishedAt: "2026-10-19T12:34:56.123456Z",
            id: "00000000-0000-4000-8000-000000000001" };
        expect(decodeCatalogCursor(encodeCatalogCursor(cursor))).toEqual(cursor);
    });

    it.each([0, 51, 1.5, "1;SELECT 1"])("rejects invalid limits before a database call", async (limit) => {
        const pool = { execute: vi.fn() };
        await expect(localCatalogPage(pool, { limit })).rejects.toThrow("Invalid catalog");
        expect(pool.execute).not.toHaveBeenCalled();
    });

    it("returns disabled without reading products", async () => {
        const pool = { execute: vi.fn().mockResolvedValue([[{ enabled: 0 }]]) };
        expect(await localCatalogPage(pool)).toEqual({ enabled: false, values: [] });
        expect(pool.execute).toHaveBeenCalledTimes(1);
    });
});
