import { describe, expect, it } from "vitest";
import type { APIContext } from "astro";
import { POST as claim } from "../pages/marketplace/v1/claims/index";
import { POST as checkout } from "../pages/marketplace/v1/checkout/index";

const id = "8089586c-ad90-4d40-a736-c6b9018cdc7f";
function context(amount: number | null, authenticated = true) {
    let transitions = 0;
    const supabase = {
        from(table: string) {
            const query = {
                select() { return query; }, eq() { return query; },
                async maybeSingle() { return { error: null, data: table === "marketplace_catalog"
                    ? { id, license_spdx: "MIT", license_revision: "1", license_acceptance_snapshot: "MIT license" }
                    : amount === null ? null : { currency: "USD", amount_minor: amount } }; },
            };
            return query;
        },
        functions: { async invoke() { transitions++; return { data: { data: { entitlementId: "owned" } }, error: null }; } },
    };
    return { transitions: () => transitions, value: {
        request: new Request("https://example.test/marketplace/v1/claims/", { method: "POST",
            headers: { "content-type": "application/json", "idempotency-key": "test-acquisition-001" },
            body: JSON.stringify({ productId: id, amount: 0 }) }),
        url: new URL("https://example.test/marketplace/v1/claims/"),
        locals: { supabase, user: authenticated ? { id: "user" } : null, correlationId: "test" },
    } as unknown as APIContext };
}

describe("marketplace acquisition HTTP boundaries", () => {
    it.each([[1999, "marketplace.purchase_unavailable"], [null, "marketplace.offer_unavailable"]] as const)(
        "does not submit a free claim for a paid or missing offer", async (amount, code) => {
            const test = context(amount);
            const response = await claim(test.value);
            expect(response.status).toBe(409);
            expect((await response.json()).error.code).toBe(code);
            expect(test.transitions()).toBe(0);
        });
    it("claims a server-confirmed free offer", async () => {
        const test = context(0);
        expect((await claim(test.value)).status).toBe(201);
        expect(test.transitions()).toBe(1);
    });
    it.each([null, 0, 1999])("disabled checkout never creates a payment or entitlement", async (amount) => {
        const test = context(amount);
        expect((await checkout(test.value)).status).toBe(409);
        expect(test.transitions()).toBe(0);
    });
    it("requires authentication before checkout", async () => {
        const test = context(1999, false);
        expect((await checkout(test.value)).status).toBe(401);
        expect(test.transitions()).toBe(0);
    });
});
