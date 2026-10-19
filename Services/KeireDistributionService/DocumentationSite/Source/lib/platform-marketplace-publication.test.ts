import { describe, expect, it } from "vitest";
import { canonicalJson, marketplaceRecords } from "./platform-marketplace-import.mjs";
import { marketplacePublicationTables } from "./platform-marketplace-publication-schema.mjs";
import { marketplaceTables } from "./platform-marketplace-schema.mjs";

describe("publication data migration", () => {
    it("selects all seven reviewed publication tables", () => {
        expect(marketplaceTables("marketplace-publication")).toBe(marketplacePublicationTables);
        const source = { project: "khjduyjamzwumhducmou", tables: Object.fromEntries(
            marketplacePublicationTables.map((table) => [`public.${table.name}`, []])) };
        expect(marketplaceRecords(source, "marketplace-publication")).toHaveLength(7);
    });

    it("compares nested diagnostic objects independently of property order", () => {
        expect(canonicalJson([{ severity: "warning", detail: { line: 3, message: "Kéire" } }]))
            .toBe(canonicalJson([{ detail: { message: "Kéire", line: 3 }, severity: "warning" }]));
        expect(canonicalJson(["windows", "linux"])).not.toBe(canonicalJson(["linux", "windows"]));
    });

    it.each([Number.MAX_SAFE_INTEGER + 1, Infinity, undefined])("rejects unsafe diagnostic values", (value) => {
        expect(() => canonicalJson([{ detail: value }])).toThrow();
    });

    it("does not collapse prototype-named diagnostic properties", () => {
        const value = JSON.parse('{"__proto__":{"message":"kept"},"constructor":"kept"}');
        expect(JSON.parse(canonicalJson(value))).toEqual(value);
    });
});
