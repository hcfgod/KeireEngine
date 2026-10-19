import { describe, expect, it, vi } from "vitest";
import { claimLocalProduct } from "./platform-claims.mjs";

const input = {
    productId: "00000000-0000-4000-8000-000000000001",
    idempotencyKey: "claim-request-identifier",
    licenseSpdx: "MIT",
    licenseRevision: "1",
    acceptedLicenseSnapshot: "Accepted license text",
};

describe("local marketplace acquisition", () => {
    it("rejects malformed acceptance before opening a transaction", async () => {
        const auth = { withSessionTransaction: vi.fn() };
        await expect(claimLocalProduct(auth, "token", { ...input, acceptedLicenseSnapshot: "" }))
            .rejects.toMatchObject({ status: 400, code: "marketplace.invalid_claim" });
        expect(auth.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("requires a transactionally verified session", async () => {
        const auth = { withSessionTransaction: vi.fn().mockResolvedValue(null) };
        await expect(claimLocalProduct(auth, "expired", input))
            .rejects.toMatchObject({ status: 401, code: "marketplace.authentication_required" });
    });

    it("returns clear purchase feedback without creating an order for a paid offer", async () => {
        const execute = vi.fn()
            .mockResolvedValueOnce([[{ enabled: 1 }]])
            .mockResolvedValueOnce([[{
                license_spdx: input.licenseSpdx,
                license_revision: input.licenseRevision,
                acceptance_snapshot: input.acceptedLicenseSnapshot,
            }]])
            .mockResolvedValueOnce([[{ id: "offer", currency: "USD", amount_minor: "499" }]]);
        const auth = {
            withSessionTransaction: async (_token, action) => action({ execute }, { userId: "fixture" }),
        };
        await expect(claimLocalProduct(auth, "token", input))
            .rejects.toMatchObject({ status: 409, code: "marketplace.purchase_unavailable" });
        expect(execute).toHaveBeenCalledTimes(3);
        expect(execute.mock.calls.every(([sql]) => sql.startsWith("SELECT"))).toBe(true);
    });
});
