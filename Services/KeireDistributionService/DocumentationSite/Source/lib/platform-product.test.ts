import { describe, expect, it, vi } from "vitest";
import { localProductDetail } from "./platform-product.mjs";

const productId = "00000000-0000-4000-8000-000000000001";

function fixture(rows) {
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(),
        release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn(),
    };
    for (const result of rows) {
        connection.execute.mockResolvedValueOnce([result]);
    }
    return { connection, pool: { getConnection: vi.fn().mockResolvedValue(connection) } };
}

describe("public MySQL product details", () => {
    it("never reads child records when the marketplace is disabled", async () => {
        const { connection, pool } = fixture([[{ enabled: 0 }]]);
        expect(await localProductDetail(pool, productId)).toEqual({ enabled: false, product: null });
        expect(connection.execute).toHaveBeenCalledTimes(1);
        expect(connection.commit).toHaveBeenCalledOnce();
        expect(connection.release).toHaveBeenCalledOnce();
    });

    it("does not expose children of an invisible product", async () => {
        const { connection, pool } = fixture([[{ enabled: 1 }], []]);
        expect(await localProductDetail(pool, productId)).toEqual({ enabled: true, product: null });
        expect(connection.execute).toHaveBeenCalledTimes(2);
        const [query, parameters] = connection.execute.mock.calls[1];
        expect(query).toContain("p.state='published'");
        expect(query).toContain("publisher.suspended_at IS NULL");
        expect(query).toContain("category.active=TRUE");
        expect(query).toContain("p.id=?");
        expect(parameters).toEqual([productId]);
    });

    it("projects public fields, preserves timestamps and normalizes pricing", async () => {
        const { connection, pool } = fixture([
            [{ enabled: 1 }], [{ id: productId, published_at: "2026-10-19 12:00:00.123456" }],
            [{ id: "version", state: "published", published_at: "2026-10-19 12:00:00.123456", platforms: ["windows"] }],
            [], [{ id: "review", created_at: "2026-10-19 12:00:00.123456", updated_at: null }],
            [{ amount_minor: "0", currency: "USD" }],
        ]);
        const result = await localProductDetail(pool, productId);
        expect(result.product.offer).toEqual({ amount_minor: 0, currency: "USD" });
        expect(result.product.versions[0].published_at).toBe("2026-10-19T12:00:00.123456Z");
        expect(result.product.versions[0].platforms).toEqual(["windows"]);
        expect(result.product.reviews[0].updated_at).toBeNull();
        expect(connection.execute.mock.calls[2][0]).not.toContain("archive_storage_path");
        expect(connection.execute.mock.calls[3][0]).toContain("approved_at IS NOT NULL");
        expect(connection.execute.mock.calls[4][0]).toContain("hidden_at IS NULL");
        expect(connection.execute.mock.calls[4][0]).not.toContain("user_id");
    });

    it("rejects malformed product identifiers without querying data", async () => {
        const { connection, pool } = fixture([]);
        await expect(localProductDetail(pool, "' OR 1=1")).rejects.toThrow("Invalid catalog product ID");
        expect(connection.execute).not.toHaveBeenCalled();
        expect(connection.rollback).toHaveBeenCalledOnce();
    });

    it("rejects unsafe prices and rolls back instead of rounding them", async () => {
        const { connection, pool } = fixture([
            [{ enabled: 1 }], [{ id: productId, published_at: null }], [], [], [],
            [{ amount_minor: "9007199254740993", currency: "USD" }],
        ]);
        await expect(localProductDetail(pool, productId)).rejects.toThrow("Invalid active product price");
        expect(connection.commit).not.toHaveBeenCalled();
        expect(connection.rollback).toHaveBeenCalledOnce();
    });
});
