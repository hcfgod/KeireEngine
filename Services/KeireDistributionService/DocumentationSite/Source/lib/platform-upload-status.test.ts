import { describe, expect, it, vi } from "vitest";
import { localUploadStatus } from "./platform-upload-status.mjs";

const uploadId = "11111111-1111-4111-8111-111111111111";

function fixture({ assurance = 2, enabled = true, visible = true, report = null } = {}) {
    const execute = vi.fn(async (sql, parameters) => {
        if (sql.includes("platform_feature_flags")) {
            return [[{ enabled }]];
        }
        if (sql.includes("SELECT upload.id")) {
            expect(parameters).toEqual([uploadId, "user"]);
            expect(sql).toContain("member.role IN ('owner','admin')");
            expect(sql).toContain("organization.deleted_at IS NULL");
            expect(sql).toContain("publisher.suspended_at IS NULL");
            return [visible ? [{ id: uploadId, version_id: "version", state: "uploaded",
                version_state: "uploaded", validation_attempts: 1 }] : []];
        }
        expect(parameters).toEqual([uploadId]);
        return [report ? [report] : []];
    });
    const authentication = {
        withSessionTransaction: vi.fn(async (_token, action) => action({ execute }, { userId: "user", assurance })),
    };
    return { execute, authentication };
}

describe("private upload status", () => {
    it("reports pending validation without exposing storage or report diagnostics", async () => {
        const test = fixture();
        expect(await localUploadStatus(test.authentication, "token", uploadId)).toEqual({
            uploadId, state: "uploaded", validationAttempts: 1, versionId: "version",
            versionState: "uploaded", validation: null,
        });
    });

    it("normalizes MySQL report values to the existing status contract", async () => {
        const test = fixture({ report: { passed: 0, diagnostics: '[{"message":"private"}]',
            validator_version: "1", completed_at: "2026-10-20 01:02:03.000000" } });
        const result = await localUploadStatus(test.authentication, "token", uploadId);
        expect(result.validation).toEqual({ passed: false, diagnosticCount: 1,
            validatorVersion: "1", completedAt: "2026-10-20T01:02:03.000000Z" });
        expect(JSON.stringify(result)).not.toContain("private");
    });

    it.each([
        [{ assurance: 1 }, 403],
        [{ enabled: false }, 503],
        [{ visible: false }, 404],
    ])("denies inaccessible status before querying reports", async (options, status) => {
        const test = fixture(options);
        await expect(localUploadStatus(test.authentication, "token", uploadId)).rejects.toMatchObject({ status });
        expect(test.execute.mock.calls.some(([sql]) => sql.includes("marketplace_validation_reports"))).toBe(false);
    });

    it("rejects malformed identifiers before database access", async () => {
        const test = fixture();
        await expect(localUploadStatus(test.authentication, "token", "-".repeat(36)))
            .rejects.toMatchObject({ status: 400 });
        expect(test.authentication.withSessionTransaction).not.toHaveBeenCalled();
    });

    it("requires an active session", async () => {
        await expect(localUploadStatus({ withSessionTransaction: async () => null }, "expired", uploadId))
            .rejects.toMatchObject({ status: 401 });
    });
});
