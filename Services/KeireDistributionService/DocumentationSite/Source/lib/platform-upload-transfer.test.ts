import { describe, expect, it, vi } from "vitest";
import { transferLocalUpload } from "./platform-upload-transfer.mjs";

const uploadId = "11111111-1111-4111-8111-111111111111";

function fixture() {
    const upload = { id: uploadId, version_id: "version", state: "created", version_state: "draft",
        expected_sha256: "a".repeat(64), expected_size_bytes: "10", unexpired: 1 };
    const execute = vi.fn(async (sql: string) => {
        if (sql.includes("platform_feature_flags")) {
            return [[{ enabled: 1 }]];
        }
        if (sql.includes("SELECT upload.id")) {
            return [[upload]];
        }
        return [{ affectedRows: 1 }];
    });
    let active = true;
    const authentication = {
        withSessionTransaction: vi.fn(async (_token, action) => active
            ? action({ execute }, { userId: "user", assurance: 2 }) : null),
    };
    return { upload, execute, authentication, revoke: () => { active = false; } };
}

describe("private upload transfer", () => {
    it("returns a recorded completion without reading or rewriting bytes", async () => {
        const test = fixture();
        Object.assign(test.upload, { state: "uploaded", version_state: "uploaded", uploaded_at: "2026-10-20", unexpired: 0 });
        const original = test.execute.getMockImplementation();
        test.execute.mockImplementation(async (sql) => sql.includes("SELECT id FROM platform_audit_events")
            ? [[{ id: "receipt" }]] : original(sql));
        const put = vi.fn();
        expect(await transferLocalUpload(test.authentication, "token", uploadId, { put }, {}))
            .toEqual({ uploadId, versionId: "version", state: "uploaded" });
        expect(put).not.toHaveBeenCalled();
        expect(test.execute.mock.calls.some(([sql]) => sql.startsWith("UPDATE"))).toBe(false);
    });

    it("uses only the reserved identity and queues after storage verification", async () => {
        const test = fixture();
        const chunks = {};
        const put = vi.fn(async () => {
            expect(test.execute.mock.calls.some(([sql]) => sql.includes("state='uploaded'"))).toBe(false);
        });
        expect(await transferLocalUpload(test.authentication, "token", uploadId, { put }, chunks))
            .toEqual({ uploadId, versionId: "version", state: "uploaded" });
        expect(put).toHaveBeenCalledWith({ sha256: "a".repeat(64), sizeBytes: 10 }, chunks);
        expect(test.authentication.withSessionTransaction).toHaveBeenCalledTimes(2);
    });

    it("never queues a failed or interrupted blob transfer", async () => {
        const test = fixture();
        const failure = new Error("Hash mismatch or client disconnected");
        await expect(transferLocalUpload(test.authentication, "token", uploadId,
            { put: vi.fn().mockRejectedValue(failure) }, {})).rejects.toBe(failure);
        expect(test.authentication.withSessionTransaction).toHaveBeenCalledTimes(1);
        expect(test.execute.mock.calls.some(([sql]) => sql.includes("state='uploaded'"))).toBe(false);
    });

    it("rechecks authorization after bytes arrive rather than trusting the initial session", async () => {
        const test = fixture();
        await expect(transferLocalUpload(test.authentication, "token", uploadId,
            { put: async () => test.revoke() }, {})).rejects.toMatchObject({ status: 401 });
        expect(test.execute.mock.calls.some(([sql]) => sql.includes("state='uploaded'"))).toBe(false);
    });

    it("rejects expired reservations without consuming bytes", async () => {
        const test = fixture();
        test.upload.unexpired = 0;
        const put = vi.fn();
        await expect(transferLocalUpload(test.authentication, "token", uploadId, { put }, {}))
            .rejects.toMatchObject({ status: 409 });
        expect(put).not.toHaveBeenCalled();
    });
});
