import { describe, expect, it } from "vitest";
import { marketplaceContentTables } from "./platform-marketplace-content-schema.mjs";
import { marketplaceRecords, verifyMarketplaceSourceColumns } from "./platform-marketplace-import.mjs";
import { marketplaceTables } from "./platform-marketplace-schema.mjs";

function snapshot() {
    return { project: "khjduyjamzwumhducmou", tables: Object.fromEntries(
        marketplaceContentTables.map((table) => [`public.${table.name}`, [] as Record<string, unknown>[]])) };
}

function schema() {
    return { columns: marketplaceContentTables.flatMap((table) => Object.entries(table.columns).map(([name, type]) => ({
        table_schema: "public", table_name: table.name, column_name: name,
        udt_name: type.startsWith("CHAR(36)") ? "uuid" : type.startsWith("INT") ? "int4" :
            type.startsWith("BOOLEAN") ? "bool" : type.startsWith("DATETIME") ? "timestamptz" : "text",
        is_nullable: type.endsWith("NOT NULL") ? "NO" : "YES",
    }))) };
}

describe("marketplace content mapping", () => {
    it("validates source schema even when all rows are empty", () => {
        const source = schema();
        expect(() => verifyMarketplaceSourceColumns(source, "marketplace-content")).not.toThrow();
        source.columns.pop();
        expect(() => verifyMarketplaceSourceColumns(source, "marketplace-content")).toThrow("Source schema changed");
    });

    it("rejects source type or nullability changes", () => {
        const source = schema();
        source.columns[0].udt_name = "jsonb";
        expect(() => verifyMarketplaceSourceColumns(source, "marketplace-content")).toThrow("column mapping changed");
        source.columns[0].udt_name = "uuid";
        source.columns[0].is_nullable = "YES";
        expect(() => verifyMarketplaceSourceColumns(source, "marketplace-content")).toThrow("column mapping changed");
    });

    it("selects all four reviewed content tables", () => {
        expect(marketplaceTables("marketplace-content")).toBe(marketplaceContentTables);
        expect(marketplaceRecords(snapshot(), "marketplace-content")).toHaveLength(4);
    });

    it("preserves dependency ranges and conflict flags", () => {
        const source = snapshot();
        source.tables["public.marketplace_version_dependencies"].push({
            version_id: "12345678-1234-1234-1234-123456789abc",
            package_id: "keire.example", version_range: ">=0.4.7 <1.0.0", conflict: true,
        });
        const records = marketplaceRecords(source, "marketplace-content");
        expect(records[2].values[0].slice(1)).toEqual(["keire.example", ">=0.4.7 <1.0.0", 1]);
    });

    it("preserves nullable media metadata and unicode descriptions", () => {
        const source = snapshot();
        const id = "12345678-1234-1234-1234-123456789abc";
        source.tables["public.marketplace_product_media"].push({
            id, product_id: id, storage_path: "images/preview.png", media_type: "image/png",
            alt_text: "Kéire — aperçu", width: null, height: null, sort_order: 0, approved_at: null,
            sha256: "a".repeat(64),
        });
        expect(marketplaceRecords(source, "marketplace-content")[0].values[0].slice(4, 9))
            .toEqual(["Kéire — aperçu", null, null, 0, null]);
    });
});
