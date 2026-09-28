import type { SupabaseClient } from "@supabase/supabase-js";
import { describe, expect, it, vi } from "vitest";
import { loadMarketplaceCatalog } from "./marketplace";

function publicClient(overrides: Record<string, unknown> = {}) {
    const results: Record<string, unknown> = {
        platform_feature_flags: { data: { enabled: true }, error: null },
        marketplace_catalog: { data: [{ slug: "free-asset" }], error: null },
        marketplace_categories: { data: [{ slug: "tools", display_name: "Tools" }], error: null },
        ...overrides,
    };
    const calls: [string, string, unknown[]][] = [];
    const from = vi.fn((table: string) => {
        const query: Record<string, unknown> = {};
        for (const method of ["select", "eq", "order", "limit", "ilike", "maybeSingle"]) {
            query[method] = (...args: unknown[]) => { calls.push([table, method, args]); return query; };
        }
        query.then = (resolve: (value: unknown) => unknown, reject: (reason: unknown) => unknown) =>
            (results[table] instanceof Error ? Promise.reject(results[table]) : Promise.resolve(results[table])).then(resolve, reject);
        return query;
    });
    return { client: { from } as unknown as SupabaseClient, from, calls };
}

describe("public marketplace catalog", () => {
    it("loads published assets without an authenticated user or paid checkout flag", async () => {
        const { client, from } = publicClient();
        const result = await loadMarketplaceCatalog(client);
        expect(result.status).toBe("ready");
        expect(result.products).toEqual([{ slug: "free-asset" }]);
        expect(from.mock.calls.map(([table]) => table)).toEqual([
            "platform_feature_flags", "marketplace_catalog", "marketplace_categories",
        ]);
    });
    it("distinguishes missing configuration and query failures from an empty catalog", async () => {
        expect((await loadMarketplaceCatalog(null)).status).toBe("unavailable");
        for (const table of ["platform_feature_flags", "marketplace_catalog", "marketplace_categories"]) {
            const { client } = publicClient({ [table]: { data: null, error: { message: "unavailable" } } });
            expect((await loadMarketplaceCatalog(client)).status).toBe("unavailable");
        }
        const { client } = publicClient({ marketplace_catalog: { data: [], error: null } });
        expect(await loadMarketplaceCatalog(client)).toMatchObject({ status: "ready", products: [] });
    });
    it("preserves the administrative disable switch and fails closed on transport errors", async () => {
        const disabled = publicClient({ platform_feature_flags: { data: { enabled: false }, error: null } });
        expect((await loadMarketplaceCatalog(disabled.client)).status).toBe("disabled");
        expect(disabled.from).toHaveBeenCalledTimes(1);
        const failing = publicClient({ platform_feature_flags: new Error("network") });
        expect((await loadMarketplaceCatalog(failing.client)).status).toBe("unavailable");
    });
    it("applies literal search and category filters", async () => {
        const { client, calls } = publicClient();
        await loadMarketplaceCatalog(client, "50%_pack", "tools");
        expect(calls).toContainEqual(["marketplace_catalog", "ilike", ["display_name", "%50\\%\\_pack%"]]);
        expect(calls).toContainEqual(["marketplace_catalog", "eq", ["category_slug", "tools"]]);
    });
});
