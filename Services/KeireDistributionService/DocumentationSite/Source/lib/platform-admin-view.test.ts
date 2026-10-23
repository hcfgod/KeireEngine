import { describe, expect, it, vi } from "vitest";
import { localAdminOverview } from "./platform-admin-view.mjs";
import { handleLocalAdminRequest } from "./platform-admin-http.mjs";

function fixture(overrides: Record<string, unknown> = {}) {
    const staff = { role: "administrator", active: 1, revoked_at: null, ...overrides };
    const execute = vi.fn(async (sql: string) => {
        if (sql.includes("FOR SHARE")) {
            return [[staff]];
        }
        return [[]];
    });
    const session = { userId: "staff-user", assurance: 2 };
    const authentication = {
        session: vi.fn(async () => session),
        withSessionTransaction: vi.fn(async (_token: string, operation: Function) => operation({ execute }, session)),
    };
    return { execute, session, authentication };
}

const request = (method = "GET", extra: Record<string, string> = {}) => new Request("https://example.test/admin/marketplace/", {
    method, headers: { cookie: "__Host-keire-session=" + "a".repeat(43), ...extra },
});

describe("MySQL staff read model", () => {
    it("requires an authenticated transaction before querying private queues", async () => {
        const authentication = { withSessionTransaction: vi.fn(async () => null) };
        await expect(localAdminOverview(authentication, "expired")).rejects.toMatchObject({ status: 401 });
    });

    it.each([{ active: 0 }, { revoked_at: "2026-01-01" }, { role: "owner" }])("denies inactive, revoked or non-staff roles: %j", async (staff) => {
        const test = fixture(staff);
        await expect(localAdminOverview(test.authentication, "token")).rejects.toMatchObject({ status: 404 });
        expect(test.execute).toHaveBeenCalledTimes(1);
    });

    it("does not read staff membership or private data for email-only assurance", async () => {
        const test = fixture();
        test.session.assurance = 1;
        await expect(localAdminOverview(test.authentication, "token")).rejects.toMatchObject({ status: 403 });
        expect(test.execute).not.toHaveBeenCalled();
    });

    it("returns bounded empty queues and advertises only implemented operations", async () => {
        const test = fixture({ role: "moderator" });
        const result = await localAdminOverview(test.authentication, "token");
        expect(result.role).toBe("moderator");
        expect(result.availableOperations).toEqual(["submission.decide", "application.decide", "report.decide", "staff.set", "feature.set"]);
        expect(result.evidenceAvailable).toBe(false);
        expect(result.versions).toEqual([]);
        expect(result.limitedQueues).toEqual([]);
        expect(test.execute.mock.calls[0][0]).toContain("FOR SHARE");
        expect(test.execute.mock.calls.slice(1).every(([sql]) => sql.includes("LIMIT"))).toBe(true);
    });

    it("reports truncated queues and normalizes SQL UTC timestamps", async () => {
        const test = fixture();
        test.execute.mockImplementation(async (sql: string) => {
            if (sql.includes("FOR SHARE")) {
                return [[{ role: "administrator", active: 1, revoked_at: null }]];
            }
            if (sql.includes("FROM publisher_applications")) {
                return [Array.from({ length: 101 }, (_, id) => ({ id, submitted_at: "2026-10-22 10:11:12.000000" }))];
            }
            return [[]];
        });
        const result = await localAdminOverview(test.authentication, "token");
        expect(result.applications).toHaveLength(100);
        expect(result.limitedQueues).toEqual(["applications"]);
        expect(result.applications[0].submitted_at).toBe("2026-10-22T10:11:12.000000Z");
    });
});

describe("MySQL staff page adapter", () => {
    it("ignores unrelated routes", async () => {
        expect(await handleLocalAdminRequest({}, new Request("https://example.test/account/"), vi.fn())).toBeNull();
    });

    it("rejects methods and bearer credentials without reading private data", async () => {
        const test = fixture();
        expect((await handleLocalAdminRequest(test, request("POST"), vi.fn())).status).toBe(405);
        expect((await handleLocalAdminRequest(test, request("GET", { authorization: "Bearer token" }), vi.fn())).status).toBe(401);
        expect(test.authentication.session).not.toHaveBeenCalled();
    });

    it("redirects anonymous users and incomplete verification before private queries", async () => {
        const test = fixture();
        test.authentication.session.mockResolvedValueOnce(null as any);
        const anonymous = await handleLocalAdminRequest(test, request(), vi.fn());
        expect(anonymous.status).toBe(303);
        expect(anonymous.headers.get("location")).toContain("/account/sign-in/");
        test.session.assurance = 1;
        const challenge = await handleLocalAdminRequest(test, request(), vi.fn());
        expect(challenge.headers.get("location")).toContain("/account/mfa/challenge/");
        expect(test.execute).not.toHaveBeenCalled();
    });

    it("revalidates staff status and keeps private pages noncacheable", async () => {
        const test = fixture();
        const render = vi.fn(async () => new Response("Staff page"));
        const response = await handleLocalAdminRequest(test, request(), render);
        expect(response.status).toBe(200);
        expect(response.headers.get("cache-control")).toBe("private, no-store");
        expect(response.headers.get("vary")).toBe("Cookie, Authorization");
        expect(response.headers.get("referrer-policy")).toBe("no-referrer");
        expect(render.mock.calls[0][0].adminView.role).toBe("administrator");
    });

    it("hides membership denial and does not leak database errors", async () => {
        const test = fixture({ active: 0 });
        const render = vi.fn();
        expect((await handleLocalAdminRequest(test, request(), render)).status).toBe(404);
        test.authentication.withSessionTransaction.mockRejectedValueOnce(new Error("private connection details"));
        const failed = await handleLocalAdminRequest(test, request(), render);
        expect(failed.status).toBe(503);
        expect(await failed.text()).not.toContain("private connection details");
        expect(render).not.toHaveBeenCalled();
    });
});
