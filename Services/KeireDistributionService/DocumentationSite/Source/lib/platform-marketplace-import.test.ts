import { describe, expect, it } from "vitest";
import { marketplaceCoreTables, marketplaceTableSql } from "./platform-marketplace-schema.mjs";
import { marketplaceRecords } from "./platform-marketplace-import.mjs";

function snapshot() {
    return { project: "khjduyjamzwumhducmou", tables: Object.fromEntries(
        marketplaceCoreTables.map((table) => [`public.${table.name}`, [] as Record<string, unknown>[]])) };
}

function offer() {
    return { id: "12345678-1234-1234-1234-123456789abc", product_id: "12345678-1234-1234-1234-123456789abd",
        currency: "USD", amount_minor: 0, active: true, created_at: "2026-10-19T12:00:00.123456Z" };
}

describe("marketplace core import", () => {
    it("normalizes exact timestamp, bigint and boolean values", () => {
        const source = snapshot();
        source.tables["public.marketplace_offers"].push(offer());
        expect(marketplaceRecords(source).at(-1)?.values[0].slice(2))
            .toEqual(["USD", "0", 1, "2026-10-19 12:00:00.123456"]);
    });

    it("refuses unexpected projects and missing source tables", () => {
        expect(() => marketplaceRecords({ ...snapshot(), project: "other" })).toThrow("Unexpected");
        expect(() => marketplaceRecords({ project: snapshot().project, tables: {} })).toThrow("Missing source");
    });

    it("refuses dropped or additional columns", () => {
        const source = snapshot();
        source.tables["public.marketplace_offers"].push({ ...offer(), new_column: true });
        expect(() => marketplaceRecords(source)).toThrow("Source columns changed");
    });

    it.each([Number.MAX_SAFE_INTEGER + 1, "9223372036854775808", "01", 0.5])("rejects unsafe integer %s", (amount) => {
        const source = snapshot();
        source.tables["public.marketplace_offers"].push({ ...offer(), amount_minor: amount });
        expect(() => marketplaceRecords(source)).toThrow();
    });

    it("refuses silent text truncation and boolean coercion", () => {
        const source = snapshot();
        source.tables["public.marketplace_offers"].push({ ...offer(), currency: "USDD" });
        expect(() => marketplaceRecords(source)).toThrow("destination bound");
        source.tables["public.marketplace_offers"] = [{ ...offer(), active: "false" }];
        expect(() => marketplaceRecords(source)).toThrow("boolean");
    });

    it("does not generate SQL from caller-supplied table definitions", () => {
        expect(() => marketplaceTableSql({ name: "unexpected", columns: {}, extra: [] })).toThrow("reviewed");
        expect(marketplaceTableSql(marketplaceCoreTables[0])).toContain("UNIQUE KEY organization_active_slug");
    });
});
