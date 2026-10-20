import { describe, expect, it, vi } from "vitest";
import { submitLocalVersion } from "./platform-version-submit.mjs";

const versionId = "11111111-1111-4111-8111-111111111111";

function fixture({ assurance = 2, enabled = true, authorized = true, report = true, existing = null } = {}) {
    const execute = vi.fn(async (sql) => {
        if (sql.includes("FROM platform_feature_flags")) {
            return [[{ enabled }]];
        }
        if (sql.includes("FROM marketplace_product_versions")) {
            return [authorized ? [{ id: versionId, product_id: "product", archive_sha256: "a".repeat(64) }] : []];
        }
        if (sql.includes("FROM marketplace_validation_reports")) {
            return [report ? [{ id: "report" }] : []];
        }
        if (sql.includes("FROM marketplace_submissions")) {
            return [existing ? [existing] : []];
        }
        return [{ affectedRows: 1 }];
    });
    const authentication = {
        withSessionTransaction: vi.fn((_token, action) => action({ execute }, { userId: "user", assurance })),
    };
    return { authentication, execute };
}

describe("local validated release submission", () => {
    it("binds the report and state changes to the authenticated transaction", async () => {
        const { authentication, execute } = fixture();
        const result = await submitLocalVersion(authentication, "session", versionId);
        expect(result.state).toBe("submitted");
        expect(authentication.withSessionTransaction).toHaveBeenCalledOnce();
        const insert = execute.mock.calls.find(([sql]) => sql.includes("INSERT INTO marketplace_submissions"));
        expect(insert?.[1]).toEqual([result.submissionId, versionId, "report", "user"]);
        expect(execute.mock.calls.at(-1)?.[0]).toContain("INSERT INTO platform_audit_events");
        expect(execute.mock.calls.at(-1)?.[0]).toContain("correlation_id");
        expect(execute.mock.calls.at(-1)?.[1][2]).toMatch(/^[0-9a-f-]{36}$/);
    });

    it.each([
        [{ assurance: 1 }, "auth.mfa_required"],
        [{ enabled: false }, "publisher.disabled"],
        [{ authorized: false }, "publisher.version_not_submittable"],
        [{ report: false }, "publisher.validation_required"],
        [{ existing: { id: "submission", state: "in_review" } }, "publisher.submission_already_active"],
    ])("rejects invalid submission prerequisites without mutations", async (options, code) => {
        const { authentication, execute } = fixture(options);
        await expect(submitLocalVersion(authentication, "session", versionId)).rejects.toMatchObject({ code });
        expect(execute.mock.calls.every(([sql]) => sql.startsWith("SELECT"))).toBe(true);
    });

    it("resubmits requested changes using the existing submission identity", async () => {
        const { authentication, execute } = fixture({ existing: { id: "submission", state: "changes_requested" } });
        expect(await submitLocalVersion(authentication, "session", versionId))
            .toEqual({ submissionId: "submission", state: "submitted" });
        expect(execute.mock.calls.some(([sql]) => sql.includes("INSERT INTO marketplace_submissions"))).toBe(false);
        expect(execute.mock.calls.find(([sql]) => sql.includes("UPDATE marketplace_submissions"))?.[0])
            .toContain("assigned_to=NULL");
    });

    it("propagates audit failure so the owning session transaction rolls back", async () => {
        const { authentication, execute } = fixture();
        const original = execute.getMockImplementation();
        execute.mockImplementation(async (sql) => {
            if (sql.includes("INSERT INTO platform_audit_events")) {
                throw new Error("audit failed");
            }
            return original(sql);
        });
        await expect(submitLocalVersion(authentication, "session", versionId)).rejects.toThrow("audit failed");
    });
});
