import { describe, expect, it, vi } from "vitest";
import { saveLocalPublisherApplication } from "./platform-publisher-applications.mjs";

const input = {
    organizationId: "11111111-1111-4111-8111-111111111111",
    publicName: "Example publisher",
    statement: "A publisher application with sufficient detail.",
};

describe("local publisher application boundaries", () => {
    it("rejects malformed identifiers, content, actions and links before database access", async () => {
        const authentication = { withSessionTransaction: vi.fn() };
        for (const invalid of [null, {}, { ...input, applicationId: "unknown" },
            { ...input, organizationId: 123 }, { ...input, publicName: " " },
            { ...input, statement: "too short" }, { ...input, action: "approve" },
            { ...input, websiteUrl: "http://example.com" }, { ...input, portfolioUrl: "https://user:pass@example.com" }]) {
            await expect(saveLocalPublisherApplication(authentication, "token", invalid))
                .rejects.toMatchObject({ status: 400 });
        }
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("rejects missing sessions and insufficient assurance", async () => {
        await expect(saveLocalPublisherApplication({ withSessionTransaction: async () => null }, "token", input))
            .rejects.toMatchObject({ status: 401 });
        const execute = vi.fn();
        await expect(saveLocalPublisherApplication({
            withSessionTransaction: async (_token, action) => action({ execute }, { assurance: 1 }),
        }, "token", input)).rejects.toMatchObject({ status: 403 });
        expect(execute).not.toHaveBeenCalled();
    });
});
