import { describe, expect, it, vi } from "vitest";
import { localLibraryPage } from "./platform-library.mjs";

const userId = "00000000-0000-4000-8000-000000000001";
const organizationId = "00000000-0000-4000-8000-000000000002";
const session = { userId, assurance: 1 };

describe("MySQL library boundaries", () => {
    it("requires verified local session state before querying", async () => {
        const pool = { execute: vi.fn() };
        await expect(localLibraryPage(pool, null)).rejects.toThrow("authenticated");
        expect(pool.execute).not.toHaveBeenCalled();
    });

    it("scopes personal ownership and preserves microsecond cursor parameters", async () => {
        const pool = { execute: vi.fn().mockResolvedValue([[]]) };
        await localLibraryPage(pool, session, { cursor: { id: organizationId, grantedAt: "2026-10-19T12:34:56.123456Z" } });
        const [sql, values] = pool.execute.mock.calls[0];
        expect(sql).toContain("e.organization_id IS NULL AND e.user_id=account.id");
        expect(sql).toContain("e.revoked_at IS NULL");
        expect(sql).toContain("account.deleted_at IS NULL");
        expect(sql).toContain("LIMIT 25");
        expect(values).toEqual([userId, "2026-10-19 12:34:56.123456", "2026-10-19 12:34:56.123456", organizationId]);
    });

    it("requires membership in a non-deleted organization in the same query", async () => {
        const pool = { execute: vi.fn().mockResolvedValue([[]]) };
        await localLibraryPage(pool, session, { organizationId });
        const [sql, values] = pool.execute.mock.calls[0];
        expect(sql).toContain("member.user_id=account.id AND organization.deleted_at IS NULL");
        expect(values).toEqual([userId, organizationId]);
    });

    it("rejects malformed cursors and organization identifiers before querying", async () => {
        const pool = { execute: vi.fn() };
        await expect(localLibraryPage(pool, session, { organizationId: "not-a-uuid" })).rejects.toThrow("organization");
        await expect(localLibraryPage(pool, session, { cursor: { id: userId, grantedAt: "invalid" } })).rejects.toThrow();
        expect(pool.execute).not.toHaveBeenCalled();
    });
});
