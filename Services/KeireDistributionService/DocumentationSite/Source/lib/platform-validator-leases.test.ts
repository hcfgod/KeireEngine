import { describe, expect, it, vi } from "vitest";
import { leaseValidationUpload, renewValidationLease } from "./platform-validator-leases.mjs";

const uploadId = "11111111-1111-4111-8111-111111111111";
const versionId = "22222222-2222-4222-8222-222222222222";
const deadline = "2030-01-01T00:15:00.000000Z";

function fixture({ expired = [], available = [], renewable = [], auditFailure = false } = {}) {
    const connection = {
        beginTransaction: vi.fn(),
        commit: vi.fn(),
        rollback: vi.fn(),
        release: vi.fn(),
        destroy: vi.fn(),
        execute: vi.fn(async (sql, _parameters) => {
            if (sql.includes("ORDER BY upload.lease_expires_at")) return [expired];
            if (sql.includes("ORDER BY upload.uploaded_at")) return [available];
            if (sql.includes("SELECT id FROM marketplace_uploads")) return [renewable];
            if (sql.includes("DATE_FORMAT")) return [[{ expires_at: deadline, validation_attempts: 1 }]];
            if (auditFailure && sql.includes("INSERT INTO platform_audit_events")) throw new Error("Audit unavailable");
            return [{ affectedRows: 1 }];
        }),
    };
    return { connection, pool: { getConnection: vi.fn(async () => connection) } };
}

describe("internal validator queue leases", () => {
    it("returns an empty queue without incrementing any attempts", async () => {
        const { pool, connection } = fixture();
        expect(await leaseValidationUpload(pool, "worker-1")).toBeNull();
        expect(connection.execute.mock.calls.some(([sql]) => sql.startsWith("UPDATE"))).toBe(false);
        expect(connection.commit).toHaveBeenCalledOnce();
    });

    it("leases exact storage identity and commits its state and audit together", async () => {
        const { pool, connection } = fixture({ available: [{
            id: uploadId, version_id: versionId, storage_bucket: "marketplace-quarantine",
            storage_path: "package/path", expected_size_bytes: "8388608", expected_sha256: "a".repeat(64),
        }] });
        expect(await leaseValidationUpload(pool, "worker-1", 60)).toEqual({
            uploadId, versionId, storageBucket: "marketplace-quarantine", storagePath: "package/path",
            expectedSizeBytes: 8388608, expectedSha256: "a".repeat(64), leaseExpiresAt: deadline,
            validationAttempt: 1,
            evidenceStoragePath: `${uploadId}/review-evidence-${"a".repeat(64)}.json`,
        });
        expect(connection.execute.mock.calls.find(([sql]) => sql.includes("validation_attempts=validation_attempts+1"))?.[1])
            .toEqual(["worker-1", 60, uploadId]);
        expect(connection.commit).toHaveBeenCalledOnce();
    });

    it("recovers expired work and stops after the fifth attempt", async () => {
        const { pool, connection } = fixture({ expired: [
            { id: uploadId, version_id: versionId, validation_attempts: 4, version_state: "validating" },
            { id: "exhausted", version_id: "exhausted-version", validation_attempts: 5, version_state: "validating" },
        ] });
        await leaseValidationUpload(pool, "worker-1");
        const changes = connection.execute.mock.calls.filter(([sql]) => sql.startsWith("UPDATE")).map(([, parameters]) => parameters);
        expect(changes).toEqual([
            ["uploaded", uploadId], ["uploaded", versionId],
            ["failed", "exhausted"], ["validation_failed", "exhausted-version"],
        ]);
        const audits = connection.execute.mock.calls.filter(([sql]) => sql.includes("INSERT INTO platform_audit_events"));
        expect(audits).toHaveLength(1);
        expect(audits[0][1][0]).toBe("marketplace.validation.retry_exhausted");
    });

    it("rolls recovery back when its audit cannot be written", async () => {
        const { pool, connection } = fixture({ auditFailure: true, expired: [
            { id: uploadId, version_id: versionId, validation_attempts: 5, version_state: "validating" },
        ] });
        await expect(leaseValidationUpload(pool, "worker-1")).rejects.toThrow("Audit unavailable");
        expect(connection.rollback).toHaveBeenCalledOnce();
        expect(connection.commit).not.toHaveBeenCalled();
    });

    it("rejects stale or foreign renewal without changing the upload", async () => {
        const { pool, connection } = fixture();
        await expect(renewValidationLease(pool, uploadId, "worker-2")).rejects.toThrow("lease was lost");
        expect(connection.execute.mock.calls.some(([sql]) => sql.startsWith("UPDATE"))).toBe(false);
        expect(connection.rollback).toHaveBeenCalledOnce();
    });

    it("renews an owned live lease without consuming an attempt", async () => {
        const { pool, connection } = fixture({ renewable: [{ id: uploadId }] });
        expect(await renewValidationLease(pool, uploadId, "worker-1", 120)).toEqual({ leaseExpiresAt: deadline });
        expect(connection.execute.mock.calls.some(([sql]) => sql.includes("validation_attempts="))).toBe(false);
        expect(connection.commit).toHaveBeenCalledOnce();
    });

    it.each([["x", 900], ["worker-1", 59], ["worker-1", 1801], ["worker-1", 60.5]])(
        "rejects invalid worker or duration before opening a transaction", async (worker, seconds) => {
            const { pool } = fixture();
            await expect(leaseValidationUpload(pool, worker, seconds)).rejects.toThrow("parameters");
            expect(pool.getConnection).not.toHaveBeenCalled();
        });
});
