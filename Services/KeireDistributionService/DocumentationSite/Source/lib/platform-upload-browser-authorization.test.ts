import { describe, expect, it, vi } from "vitest";
import { browserUploadAuthorization } from "./platform-upload-browser-authorization.mjs";

const uploadId = "11111111-1111-4111-8111-111111111111";
const cookie = `__Host-keire-session=${"a".repeat(43)}`;
const request = (headers) => new Request("https://qa.invalid.example/publisher/v1/transfer", { headers });

describe("browser upload authorization", () => {
    it("never treats a reservation identifier as an authorization credential", async () => {
        const authentication = { withSessionTransaction: vi.fn() };
        const authorize = browserUploadAuthorization(authentication);
        await expect(authorize(request({ "x-signature": uploadId }))).rejects.toMatchObject({ status: 401 });
        await expect(authorize(request({ cookie, "x-signature": uploadId, authorization: "Bearer unsupported" })))
            .rejects.toMatchObject({ status: 401 });
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("requires a valid reservation identifier before querying data", async () => {
        const authentication = { withSessionTransaction: vi.fn() };
        await expect(browserUploadAuthorization(authentication)(request({ cookie, "x-signature": "-".repeat(36) })))
            .rejects.toMatchObject({ status: 400 });
        expect(authentication.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("resolves current membership, creator, state and expiry on every request", async () => {
        let unexpired = 1;
        const execute = vi.fn(async (sql, parameters) => {
            if (sql.includes("platform_feature_flags")) {
                return [[{ enabled: 1 }]];
            }
            expect(sql).toContain("upload.created_by=?");
            expect(sql).toContain("member.role IN ('owner','admin')");
            expect(parameters).toEqual([uploadId, "owner", "owner"]);
            return [[{ id: uploadId, state: "uploading", version_state: "draft", unexpired,
                expected_sha256: "a".repeat(64), expected_size_bytes: "6" }]];
        });
        const authentication = {
            withSessionTransaction: vi.fn(async (_token, action) => action({ execute }, { assurance: 2, userId: "owner" })),
        };
        const authorize = browserUploadAuthorization(authentication);
        const incoming = request({ cookie, "x-signature": uploadId });
        expect(await authorize(incoming)).toEqual({ uploadId, expectedSha256: "a".repeat(64), expectedSizeBytes: 6 });
        unexpired = 0;
        await expect(authorize(incoming)).rejects.toMatchObject({ status: 409 });
        expect(authentication.withSessionTransaction).toHaveBeenCalledTimes(2);
    });
});
