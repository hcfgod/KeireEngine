import { describe, expect, it, vi } from "vitest";
import { failLocalPublication } from "./platform-publication-leases.mjs";

const jobId = "11111111-1111-4111-8111-111111111111";

function fixture(attempt) {
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn(async (sql) => sql.startsWith("SELECT")
            ? [[{ id: jobId, attempt_count: attempt }]] : [{ affectedRows: 1 }]),
    };
    return { connection, pool: { getConnection: vi.fn(async () => connection) } };
}

describe("publication failure transitions", () => {
    it.each([[1, 1], [2, 5], [3, 15], [4, 60]])("backs off attempt %i for %i minutes", async (attempt, minutes) => {
        const { pool, connection } = fixture(attempt);
        expect(await failLocalPublication(pool, jobId, "worker-fixture", "publication.transient", true))
            .toEqual({ state: "retry_wait" });
        expect(connection.execute.mock.calls.find(([sql]) => sql.startsWith("UPDATE"))?.[1])
            .toEqual(["retry_wait", true, minutes, "publication.transient", jobId]);
        expect(connection.commit).toHaveBeenCalledOnce();
    });

    it("does not retry the fifth attempt", async () => {
        const { pool } = fixture(5);
        expect(await failLocalPublication(pool, jobId, "worker-fixture", "publication.transient", true))
            .toEqual({ state: "failed" });
    });

    it("rolls back state changes when audit insertion fails", async () => {
        const { pool, connection } = fixture(1);
        const execute = connection.execute.getMockImplementation();
        connection.execute.mockImplementation(async (sql) => {
            if (sql.includes("INSERT INTO platform_audit_events")) {
                throw new Error("audit unavailable");
            }
            return execute(sql);
        });
        await expect(failLocalPublication(pool, jobId, "worker-fixture", "publication.transient", true))
            .rejects.toThrow("audit unavailable");
        expect(connection.rollback).toHaveBeenCalledOnce();
        expect(connection.commit).not.toHaveBeenCalled();
    });

    it("rejects freeform error text before database access", async () => {
        const { pool } = fixture(1);
        await expect(failLocalPublication(pool, jobId, "worker-fixture", "private error text", true))
            .rejects.toMatchObject({ code: "publication.failure_invalid" });
        expect(pool.getConnection).not.toHaveBeenCalled();
    });
});
