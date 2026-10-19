import { describe, expect, it, vi } from "vitest";
import { createLocalOrganization } from "./platform-organizations.mjs";

describe("local organization authorization", () => {
    it("rejects invalid names and slugs before opening a transaction", async () => {
        const authentication = { withSessionTransaction: vi.fn() };
        for (const input of [null, {}, { slug: "bad/slash", displayName: "Name" },
            { slug: "valid-slug", displayName: " " }, { slug: "valid-slug", displayName: "a".repeat(97) }]) {
            await expect(createLocalOrganization(authentication, "token", input)).rejects.toMatchObject({ status: 400 });
        }
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("rejects an expired or revoked session", async () => {
        const authentication = { withSessionTransaction: async () => null };
        await expect(createLocalOrganization(authentication, "token", { slug: "example", displayName: "Example" }))
            .rejects.toMatchObject({ status: 401 });
    });

    it("does not accept email assurance as an authenticator or write while disabled", async () => {
        const execute = vi.fn(async () => [[{ enabled: 0 }]]);
        for (const assurance of [1, 2]) {
            const authentication = {
                withSessionTransaction: async (_token, action) => action({ execute }, { assurance }),
            };
            await expect(createLocalOrganization(authentication, "token", { slug: "example", displayName: "Example" }))
                .rejects.toMatchObject({ status: assurance === 1 ? 403 : 503 });
        }
        expect(execute).toHaveBeenCalledTimes(1);
    });
});
