import { describe, expect, it } from "vitest";
import { marketplaceAccessTables } from "./platform-marketplace-access-schema.mjs";
import { marketplaceRecords } from "./platform-marketplace-import.mjs";
import { marketplaceTables, marketplaceTableSql } from "./platform-marketplace-schema.mjs";

function snapshot() {
    return { project: "khjduyjamzwumhducmou", tables: Object.fromEntries(
        marketplaceAccessTables.map((table) => [`public.${table.name}`, [] as Record<string, unknown>[]])) };
}

describe("marketplace access migration mapping", () => {
    it("selects only the requested reviewed section", () => {
        expect(marketplaceTables("marketplace-access")).toBe(marketplaceAccessTables);
        expect(marketplaceTables().some((table) => table.name === "marketplace_orders")).toBe(false);
        expect(() => marketplaceTables("unreviewed")).toThrow("Unknown marketplace");
    });

    it("requires all access tables even when some have no rows", () => {
        const source = snapshot();
        expect(marketplaceRecords(source, "marketplace-access")).toHaveLength(5);
        delete source.tables["public.marketplace_download_grants"];
        expect(() => marketplaceRecords(source, "marketplace-access")).toThrow("Missing source table");
    });

    it("preserves personal ownership and revocation timestamps", () => {
        const source = snapshot();
        const id = "12345678-1234-1234-1234-123456789abc";
        source.tables["public.marketplace_entitlements"].push({
            id, product_id: id, user_id: id, organization_id: null, order_item_id: id,
            granted_at: "2026-10-19T12:00:00.123456Z", revoked_at: "2026-10-19T13:00:00.654321Z",
            revocation_reason: "User requested revocation",
        });
        const record = marketplaceRecords(source, "marketplace-access").find((entry) => entry.table.name === "marketplace_entitlements");
        expect(record?.values[0]).toEqual([id, id, id, null, id,
            "2026-10-19 12:00:00.123456", "2026-10-19 13:00:00.654321", "User requested revocation"]);
    });

    it("keeps access DDL distinct from authentication session tables", () => {
        const device = marketplaceAccessTables.find((table) => table.name === "oauth_device_sessions");
        expect(marketplaceTableSql(device)).toContain("device_oauth_session");
        expect(marketplaceTableSql(device)).not.toContain("platform_auth_sessions");
    });
});
