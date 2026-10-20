import { describe, expect, it, vi } from "vitest";
import { completeResumableUpload } from "./platform-upload-complete.mjs";

const uploadId = "11111111-1111-4111-8111-111111111111";
const fileId = uploadId.replaceAll("-", "");

function fixture() {
    let active = true;
    const execute = vi.fn(async (sql) => sql.includes("platform_feature_flags")
        ? [[{ enabled: 1 }]]
        : [[{ id: uploadId, state: "uploading", version_state: "draft", unexpired: 1,
            expected_sha256: "a".repeat(64), expected_size_bytes: 6 }]]);
    const authentication = {
        withSessionTransaction: async (_token, action) => active
            ? action({ execute }, { assurance: 2, userId: "owner" }) : null,
    };
    const datastore = {
        getUpload: vi.fn(async () => ({ size: 6, offset: 6, metadata: { reservationId: uploadId } })),
        read: vi.fn(),
    };
    return { authentication, datastore, revoke: () => { active = false; } };
}

describe("resumable completion boundary", () => {
    it("does not open a file if authorization expires after metadata lookup", async () => {
        const test = fixture();
        test.datastore.getUpload.mockImplementation(async () => {
            test.revoke();
            return { size: 6, offset: 6, metadata: { reservationId: uploadId } };
        });
        const put = vi.fn();
        await expect(completeResumableUpload(test.authentication, "token", uploadId, fileId, test.datastore, { put }))
            .rejects.toMatchObject({ status: 401 });
        expect(test.datastore.read).not.toHaveBeenCalled();
        expect(put).not.toHaveBeenCalled();
    });

    it("does not leak storage errors", async () => {
        const test = fixture();
        test.datastore.getUpload.mockRejectedValue(new Error("private storage path"));
        await expect(completeResumableUpload(test.authentication, "token", uploadId, fileId, test.datastore, {}))
            .rejects.toMatchObject({ status: 404, code: "publisher.transfer_unavailable" });
        expect(test.datastore.read).not.toHaveBeenCalled();
    });

    it("validates file identifiers before authorization or storage access", async () => {
        const test = fixture();
        await expect(completeResumableUpload(test.authentication, "token", uploadId, "../outside", test.datastore, {}))
            .rejects.toMatchObject({ status: 400 });
        expect(test.datastore.getUpload).not.toHaveBeenCalled();
    });
});
