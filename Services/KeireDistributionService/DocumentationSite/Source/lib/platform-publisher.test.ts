import { describe, expect, it, vi } from "vitest";
import { localPublisherPage } from "./platform-publisher.mjs";

function fixture(results) {
    const connection = { execute: vi.fn(), beginTransaction: vi.fn(), commit: vi.fn(),
        rollback: vi.fn(), release: vi.fn(), destroy: vi.fn() };
    for (const rows of results) {
        connection.execute.mockResolvedValueOnce([rows]);
    }
    return { connection, pool: { getConnection: vi.fn().mockResolvedValue(connection) } };
}

describe("MySQL publisher pages", () => {
    it("does not read publishers when the marketplace is disabled", async () => {
        const { pool, connection } = fixture([[{ enabled: 0 }]]);
        expect(await localPublisherPage(pool, "test-publisher")).toEqual({
            enabled: false, publisher: null, products: [], hasMore: false,
        });
        expect(connection.execute).toHaveBeenCalledTimes(1);
    });

    it("excludes suspended publishers before reading their products", async () => {
        const { pool, connection } = fixture([[{ enabled: 1 }], []]);
        expect((await localPublisherPage(pool, "test-publisher")).publisher).toBeNull();
        expect(connection.execute).toHaveBeenCalledTimes(2);
        expect(connection.execute.mock.calls[1][0]).toContain("suspended_at IS NULL");
        expect(connection.execute.mock.calls[1][1]).toEqual(["test-publisher"]);
    });

    it("bounds each page and scopes the catalog to the publisher", async () => {
        const id = "00000000-0000-4000-8000-000000000001";
        const products = Array.from({ length: 31 }, (_, index) => ({ id: String(index), published_at: null }));
        const { pool, connection } = fixture([
            [{ enabled: 1 }], [{ id, verified: 1, created_at: "2026-10-19 00:00:00.000001" }],
            [{ enabled: 1 }], products,
        ]);
        const result = await localPublisherPage(pool, "test-publisher");
        expect(result.products).toHaveLength(30);
        expect(result.hasMore).toBe(true);
        expect(result.publisher.verified).toBe(true);
        expect(result.publisher.created_at).toBe("2026-10-19T00:00:00.000001Z");
        expect(connection.execute.mock.calls[3][0]).toContain("p.publisher_id=?");
        expect(connection.execute.mock.calls[3][1]).toEqual([id]);
    });

    it("does not query for malformed publisher slugs", async () => {
        const { pool } = fixture([]);
        expect((await localPublisherPage(pool, "../admin")).publisher).toBeNull();
        expect(pool.getConnection).not.toHaveBeenCalled();
    });
});
