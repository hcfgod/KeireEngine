import { describe, expect, it } from "vitest";
import { marketplaceRecords } from "./platform-marketplace-import.mjs";
import { platformSupportTables } from "./platform-support-schema.mjs";
import { marketplaceSections, marketplaceTables, verifyMarketplaceTableCoverage } from "./platform-marketplace-schema.mjs";

function snapshot() {
    return { project: "khjduyjamzwumhducmou", tables: Object.fromEntries(
        platformSupportTables.map((table) => [`${table.sourceSchema ?? "public"}.${table.name}`, [] as Record<string, unknown>[]])) };
}

describe("platform support migration", () => {
    it("detects omitted or newly added domain tables", () => {
        const tables = Object.fromEntries(marketplaceSections.flatMap((section) => marketplaceTables(section)
            .map((table) => [`${table.sourceSchema ?? "public"}.${table.name}`, []])));
        expect(verifyMarketplaceTableCoverage({ tables })).toBe(35);
        tables["public.new_feature"] = [];
        expect(() => verifyMarketplaceTableCoverage({ tables })).toThrow("coverage differs");
        delete tables["public.new_feature"];
        delete tables["private.marketplace_rate_limits"];
        expect(() => verifyMarketplaceTableCoverage({ tables })).toThrow("coverage differs");
    });

    it("requires all ten tables including private rate limits", () => {
        const source = snapshot();
        expect(marketplaceRecords(source, "platform-support")).toHaveLength(10);
        delete source.tables["private.marketplace_rate_limits"];
        expect(() => marketplaceRecords(source, "platform-support")).toThrow("Missing source");
    });

    it("preserves audit metadata and explicit sequence values", () => {
        const source = snapshot();
        source.tables["public.platform_audit_events"].push({
            id: "9007199254740993", occurred_at: "2026-10-19T12:00:00.123456Z", actor_user_id: null,
            actor_session_id: null, action: "qa.migration", target_type: "fixture", target_id: "isolated",
            correlation_id: "12345678-1234-1234-1234-123456789abc", metadata: { nested: { value: true } },
        });
        const record = marketplaceRecords(source, "platform-support").find((entry) => entry.table.name === "platform_audit_events");
        expect(record?.values[0][0]).toBe("9007199254740993");
        expect(record?.values[0].at(-1)).toBe('{"nested":{"value":true}}');
        source.tables["public.platform_audit_events"][0].metadata = [];
        expect(() => marketplaceRecords(source, "platform-support")).toThrow("JSON shape");
    });

    it("rejects null IDs despite AUTO_INCREMENT destination declarations", () => {
        const source = snapshot();
        source.tables["public.website_contact_rate_limits"].push({ id: null, attempted_at: "2026-10-19T12:00:00Z", ip_hash: "a".repeat(64) });
        expect(() => marketplaceRecords(source, "platform-support")).toThrow("required marketplace value");
    });
});
