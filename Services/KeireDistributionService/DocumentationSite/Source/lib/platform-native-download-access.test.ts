import { describe, expect, it, vi } from "vitest";
import { authorizeNativeDownload } from "./platform-native-download-access.mjs";

const id = "11111111-1111-4111-8111-111111111111";
const identity = { userId: "actor", sessionId: "session" };
const input = { versionId: id, deviceSessionId: id };
const release = { version_id: id, product_id: "product", signed_publication: "signed" };
const flags = [{ enabled: 1 }, { enabled: 1 }, { enabled: 1 }];
function fixture(rows) {
    const execute = vi.fn();
    for (const row of rows) {
        execute.mockResolvedValueOnce(row);
    }
    return { execute };
}

describe("native download access", () => {
    it("binds a personal entitlement to the authenticated device", async () => {
        const connection = fixture([[flags], [[{ id }]], [[release]], [[{ id: "entitlement" }]]]);
        expect(await authorizeNativeDownload(connection, identity, input)).toEqual({
            ...release, entitlementId: "entitlement", deviceSessionId: id,
        });
        expect(connection.execute.mock.calls[1][1]).toEqual([id, "actor", "mysql:session"]);
        expect(connection.execute.mock.calls[3][1]).toEqual(["product", "actor"]);
    });

    it("requires organization membership before using its entitlement", async () => {
        const connection = fixture([[flags], [[{ id }]], [[release]], [[]]]);
        await expect(authorizeNativeDownload(connection, identity, { ...input, organizationId: id }))
            .rejects.toMatchObject({ code: "marketplace.organization_forbidden" });
        expect(connection.execute).toHaveBeenCalledTimes(4);
    });

    it.each([
        { rows: [[[{ enabled: "0" }]]], code: "unavailable" },
        { rows: [[flags], [[]]], code: "device_session_invalid" },
        { rows: [[flags], [[{ id }]], [[]]], code: "version_unavailable" },
        { rows: [[flags], [[{ id }]], [[release]], [[]]], code: "entitlement_required" },
    ])("rejects $code before returning package metadata", async ({ rows, code }) => {
        await expect(authorizeNativeDownload(fixture(rows), identity, input))
            .rejects.toMatchObject({ code: `marketplace.${code}` });
    });

    it("rejects malformed identifiers before SQL", async () => {
        const connection = fixture([]);
        await expect(authorizeNativeDownload(connection, identity, { ...input, versionId: "invalid" }))
            .rejects.toMatchObject({ status: 400 });
        expect(connection.execute).not.toHaveBeenCalled();
    });
});
