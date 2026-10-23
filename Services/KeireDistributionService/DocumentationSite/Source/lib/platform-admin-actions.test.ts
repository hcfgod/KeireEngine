import { describe, expect, it, vi } from "vitest";
import { applyLocalAdminAction, validateAdminAction } from "./platform-admin-actions.mjs";
import { handleLocalAdminActionsRequest } from "./platform-admin-actions-http.mjs";

const actor = "00000000-0000-4000-8000-000000000001";
const target = "00000000-0000-4000-8000-000000000002";
const application = { operation: "application.decide", applicationId: target, decision: "approved", decisionNote: "Reviewed safely", publisherSlug: "qa-publisher" };
const report = { operation: "report.decide", reportId: target, state: "resolved", resolutionNote: "Reviewed safely" };
const staff = { operation: "staff.set", targetUserId: target, role: "moderator", active: true, reason: "Reviewed safely" };
const feature = { operation: "feature.set", key: "marketplace_enabled", enabled: false, reason: "Reviewed safely" };

function fixture(role = "administrator") {
    const session = { userId: actor, assurance: 2 };
    const members = [{ user_id: actor, role, active: 1, revoked_at: null }];
    const execute = vi.fn(async (sql: string) => {
        if (sql.startsWith("SELECT") && sql.includes("FROM platform_staff_members")) return [members];
        if (sql.startsWith("SELECT") && sql.includes("FROM publisher_applications")) return [[{
            id: target, state: "submitted", organization_id: target, public_name: "QA Publisher",
            statement: "Reviewed publisher application.", website_url: null,
        }]];
        if (sql.startsWith("SELECT") && sql.includes("FROM organizations")) return [[{ id: target, deleted_at: null }]];
        if (sql.startsWith("SELECT") && sql.includes("FROM publishers")) return [[]];
        if (sql.startsWith("SELECT") && sql.includes("FROM marketplace_reports")) return [[{ id: target, state: "open" }]];
        if (sql.startsWith("SELECT") && sql.includes("FROM platform_accounts")) return [[{ id: target }]];
        if (sql.startsWith("SELECT") && sql.includes("FROM platform_feature_flags")) return [[{ key: feature.key }]];
        return [{ affectedRows: 1 }];
    });
    const authentication = { withSessionTransaction: vi.fn(async (_token: string, action: Function) => action({ execute }, session)) };
    return { session, members, execute, authentication };
}

const httpRequest = (input = report, headers: Record<string, string> = {}) => new Request("https://example.test/admin/marketplace/v1/actions/", {
    method: "POST", headers: { origin: "https://example.test", cookie: `__Host-keire-session=${"a".repeat(43)}`,
        "content-type": "application/json", ...headers }, body: JSON.stringify(input),
});

describe("staff action validation", () => {
    it.each([application, report, staff, feature])("accepts supported validated operation $operation", (input) => {
        expect(() => validateAdminAction(input)).not.toThrow();
    });
    it.each([
        { ...application, applicationId: "bad" }, { ...application, publisherSlug: "../bad" },
        { ...application, decisionNote: "x" }, { ...report, state: "open" }, { ...report, resolutionNote: "" },
        { ...staff, active: "true" }, { ...staff, role: "owner" }, { ...staff, reason: "x" },
        { ...feature, key: "paid_checkout_enabled", enabled: true }, { ...feature, enabled: "false" },
        { operation: "unknown" },
    ])("rejects invalid or unavailable operations %#", (input) => {
        expect(() => validateAdminAction(input)).toThrow();
    });
});

describe("staff transactional decisions", () => {
    it.each([application, report, staff, feature])("updates and audits $operation in the authenticated transaction", async (input) => {
        const test = fixture();
        await applyLocalAdminAction(test.authentication, "token", input);
        expect(test.authentication.withSessionTransaction).toHaveBeenCalledTimes(1);
        const statements = test.execute.mock.calls.map(([sql]) => sql);
        expect(statements.at(-1)).toContain("INSERT INTO platform_audit_events");
        expect(statements.some((sql) => sql.includes("FOR UPDATE"))).toBe(true);
    });
    it("rejects expired sessions and email-only assurance without mutations", async () => {
        await expect(applyLocalAdminAction({ withSessionTransaction: async () => null }, "token", report))
            .rejects.toMatchObject({ status: 401 });
        const test = fixture();
        test.session.assurance = 1;
        await expect(applyLocalAdminAction(test.authentication, "token", report)).rejects.toMatchObject({ status: 403 });
        expect(test.execute).not.toHaveBeenCalled();
    });
    it.each([staff, feature])("rejects moderator-only $operation", async (input) => {
        const test = fixture("moderator");
        await expect(applyLocalAdminAction(test.authentication, "token", input)).rejects.toMatchObject({ status: 403 });
        expect(test.execute).toHaveBeenCalledTimes(1);
    });
    it("rejects revoked membership before the mutation", async () => {
        const test = fixture();
        test.members[0].active = 0;
        await expect(applyLocalAdminAction(test.authentication, "token", report)).rejects.toMatchObject({ status: 403 });
        expect(test.execute).toHaveBeenCalledTimes(1);
    });
    it.each([{ active: false, role: "administrator" }, { active: true, role: "moderator" }])("preserves the final administrator: %j", async (change) => {
        const test = fixture();
        await expect(applyLocalAdminAction(test.authentication, "token", { ...staff, targetUserId: actor, ...change }))
            .rejects.toMatchObject({ code: "staff.last_administrator_required" });
        expect(test.execute.mock.calls[0][0]).toContain("ORDER BY user_id FOR UPDATE");
        expect(test.execute.mock.calls.every(([sql]) => sql.startsWith("SELECT"))).toBe(true);
    });
    it("allows revocation only while another active administrator remains", async () => {
        const test = fixture();
        test.members.push({ user_id: target, role: "administrator", active: 1, revoked_at: null });
        expect(await applyLocalAdminAction(test.authentication, "token", { ...staff, targetUserId: actor, active: false }))
            .toMatchObject({ active: false });
    });
    it("surfaces transactional audit failure instead of reporting a partial success", async () => {
        const test = fixture();
        const original = test.execute.getMockImplementation()!;
        test.execute.mockImplementation(async (sql: string) => {
            if (sql.includes("INSERT INTO platform_audit_events")) throw new Error("audit failed");
            return original(sql);
        });
        await expect(applyLocalAdminAction(test.authentication, "token", feature)).rejects.toThrow("audit failed");
    });
});

describe("staff actions HTTP boundary", () => {
    it.each([{ origin: "https://attacker.test" }, { origin: "null" }, { authorization: "Bearer token" }])("rejects cross-origin or bearer requests before database access: %j", async (headers) => {
        const test = fixture();
        const response = await handleLocalAdminActionsRequest(test.authentication, httpRequest(report, headers), "https://example.test", "qa");
        expect([401, 403]).toContain(response.status);
        expect(test.execute).not.toHaveBeenCalled();
    });
    it("returns audited report decisions and no-store headers", async () => {
        const test = fixture();
        const response = await handleLocalAdminActionsRequest(test.authentication, httpRequest(), "https://example.test", "qa");
        expect(response.status).toBe(200);
        expect(await response.json()).toMatchObject({ data: { report_id: target, report_state: "resolved" } });
        expect(response.headers.get("cache-control")).toBe("private, no-store");
    });
    it("redacts transaction failures", async () => {
        const test = fixture();
        test.authentication.withSessionTransaction.mockRejectedValueOnce(new Error("private database"));
        const response = await handleLocalAdminActionsRequest(test.authentication, httpRequest(), "https://example.test", "qa");
        expect(response.status).toBe(503);
        expect(await response.text()).not.toContain("private database");
    });
});
