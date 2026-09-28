import { describe, expect, it } from "vitest";
import { offerPrice, purchaseState } from "./marketplace-purchase";

describe("asset acquisition states", () => {
    it("allows only an available free offer to be claimed", () => {
        expect(purchaseState({ amount_minor: 0, currency: "USD" })).toMatchObject({ state: "free", canClaim: true });
        expect(purchaseState({ amount_minor: 0, currency: "USD" }, false, false).canClaim).toBe(false);
    });
    it("explains paid checkout without offering a free claim or implying payment", () => {
        expect(purchaseState({ amount_minor: 1999, currency: "USD" })).toMatchObject({ state: "purchase_unavailable", canClaim: false });
        expect(purchaseState({ amount_minor: 1999, currency: "USD" }).message).toContain("not been charged");
    });
    it("preserves existing ownership even if an offer disappears", () => {
        expect(purchaseState(null, true)).toMatchObject({ state: "owned", canClaim: false });
    });
    it.each([null, { amount_minor: -1, currency: "USD" }, { amount_minor: 1.5, currency: "USD" },
        { amount_minor: 0, currency: "invalid" }, { amount_minor: Number.MAX_SAFE_INTEGER + 1, currency: "USD" }])(
        "never advertises missing or invalid pricing as free", (offer) => {
            expect(purchaseState(offer)).toMatchObject({ state: "unavailable", canClaim: false });
            expect(offerPrice(offer)).toBe("Currently unavailable");
        });
    it("formats currency minor units", () => {
        expect(offerPrice({ amount_minor: 0, currency: "USD" })).toBe("Free");
        expect(offerPrice({ amount_minor: 1999, currency: "USD" })).toBe("$19.99");
        expect(offerPrice({ amount_minor: 500, currency: "JPY" })).toBe("¥500");
    });
});
