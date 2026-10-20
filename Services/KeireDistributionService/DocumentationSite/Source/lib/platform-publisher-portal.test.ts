import { describe, expect, it, vi } from "vitest";
import { localPublisherPortal } from "./platform-publisher-portal.mjs";

describe("publisher portal access", () => {
    it("requires a current authenticator-verified session before reading private data", async () => {
        await expect(localPublisherPortal({ withSessionTransaction: async () => null }, "token"))
            .rejects.toMatchObject({ status: 401 });
        const execute = vi.fn();
        await expect(localPublisherPortal({
            withSessionTransaction: async (_token, action) => action({ execute }, { assurance: 1 }),
        }, "token")).rejects.toMatchObject({ status: 403 });
        expect(execute).not.toHaveBeenCalled();
    });

    it("does not read application or publishing data when the portal is disabled", async () => {
        const execute = vi.fn().mockResolvedValue([[{ enabled: 0 }]]);
        const result = await localPublisherPortal({
            withSessionTransaction: async (_token, action) => action({ execute }, { assurance: 2 }),
        }, "token");
        expect(result.enabled).toBe(false);
        expect(result.applications).toEqual([]);
        expect(result.validationReports).toEqual([]);
        expect(execute).toHaveBeenCalledTimes(1);
    });
});
