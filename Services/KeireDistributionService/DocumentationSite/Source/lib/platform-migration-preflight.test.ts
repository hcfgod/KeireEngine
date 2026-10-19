import { describe, expect, it } from "vitest";
import { verifyMigrationRelationships, verifyMarketplaceConditionalKeys } from "./platform-migration-preflight.mjs";

function fixture() {
    return {
        database: { tables: {
            "public.products": [{ id: "product", owner: "user" }],
            "auth.users": [{ id: "user" }],
        } },
        schema: { constraints: [
            { schema: "public", table_name: "products", definition: "PRIMARY KEY (id)" },
            { schema: "public", table_name: "products", definition: "FOREIGN KEY (owner) REFERENCES auth.users(id) ON DELETE RESTRICT" },
        ] },
    };
}

describe("migration relationship preflight", () => {
    it("checks primary keys and cross-schema ownership", () => {
        const { database, schema } = fixture();
        expect(verifyMigrationRelationships(database, schema)).toEqual({ foreignKeys: 1, uniqueKeys: 1 });
    });

    it("rejects orphaned ownership without revealing row contents", () => {
        const { database, schema } = fixture();
        database.tables["auth.users"] = [];
        expect(() => verifyMigrationRelationships(database, schema)).toThrow("Orphaned migration reference");
    });

    it("rejects duplicate primary keys", () => {
        const { database, schema } = fixture();
        database.tables["public.products"].push({ id: "product", owner: "user" });
        expect(() => verifyMigrationRelationships(database, schema)).toThrow("Duplicate migration key");
    });

    it("rejects missing tables and unparsed foreign key syntax", () => {
        const { database, schema } = fixture();
        schema.constraints[1].definition += " MATCH FULL";
        expect(() => verifyMigrationRelationships(database, schema)).toThrow("Unsupported foreign key");
        expect(() => verifyMigrationRelationships({ tables: {} }, schema)).toThrow("Missing migration table");
    });

    it("preserves nullable composite foreign key semantics", () => {
        const database = { tables: { "public.child": [{ a: null, b: "missing" }], "public.parent": [] } };
        const schema = { constraints: [{ schema: "public", table_name: "child",
            definition: "FOREIGN KEY (a, b) REFERENCES parent(a, b) ON DELETE RESTRICT" }] };
        expect(verifyMigrationRelationships(database, schema).foreignKeys).toBe(1);
    });

    it("rejects integer precision loss", () => {
        const database = { tables: { "public.events": [{ id: Number.MAX_SAFE_INTEGER + 1 }] } };
        const schema = { constraints: [{ schema: "public", table_name: "events", definition: "PRIMARY KEY (id)" }] };
        expect(() => verifyMigrationRelationships(database, schema)).toThrow("Unsafe numeric migration key");
    });
});

function marketplaceFixture() {
    return { tables: Object.fromEntries([
        "profiles", "publishers", "organizations", "marketplace_offers", "marketplace_reviews",
        "marketplace_entitlements", "marketplace_uploads", "marketplace_submissions",
    ].map((name) => [`public.${name}`, [] as Record<string, unknown>[]])) };
}

describe("marketplace conditional uniqueness", () => {
    it("allows historical uploads but rejects competing active uploads", () => {
        const database = marketplaceFixture();
        const uploads = database.tables["public.marketplace_uploads"];
        uploads.push({ version_id: "version", state: "failed" }, { version_id: "version", state: "uploaded" });
        expect(verifyMarketplaceConditionalKeys(database).conditionalUniqueKeys).toBe(10);
        uploads.push({ version_id: "version", state: "leased" });
        expect(() => verifyMarketplaceConditionalKeys(database)).toThrow("Duplicate migration key");
    });

    it("rejects case-insensitive publisher collisions", () => {
        const database = marketplaceFixture();
        database.tables["public.publishers"].push({ slug: "Publisher" }, { slug: "publisher" });
        expect(() => verifyMarketplaceConditionalKeys(database)).toThrow("Duplicate migration key");
    });

    it("distinguishes organization and personal entitlements", () => {
        const database = marketplaceFixture();
        const entitlements = database.tables["public.marketplace_entitlements"];
        entitlements.push({ product_id: "product", user_id: "user", organization_id: null },
            { product_id: "product", user_id: null, organization_id: "organization" });
        expect(verifyMarketplaceConditionalKeys(database).conditionalUniqueKeys).toBe(10);
        entitlements.push({ product_id: "product", user_id: "user", organization_id: null });
        expect(() => verifyMarketplaceConditionalKeys(database)).toThrow("Duplicate migration key");
    });
});
