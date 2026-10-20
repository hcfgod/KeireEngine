import { describe, expect, it, vi } from "vitest";
import { createValidatorReportFixture } from "../../scripts/validator-report-fixture.mjs";
import { completeValidationUpload } from "./platform-validator-complete.mjs";

function fixture() {
    const signed = createValidatorReportFixture();
    Object.assign(signed.upload, { state: "leased", lease_owner: "worker-test", lease_live: 1 });
    const state = { receipt: null, live: 1, auditFailure: false };
    const connection = {
        beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn(),
        execute: vi.fn(async (sql, _parameters) => {
            if (sql.includes("SELECT upload.*")) return [[signed.upload]];
            if (sql.includes("SELECT report.id")) return [state.receipt ? [state.receipt] : []];
            if (sql.includes("SELECT * FROM marketplace_validator_attestation_keys")) return [[signed.trustedKey]];
            if (sql.includes("SELECT lease_expires_at")) return [[{ lease_live: state.live }]];
            if (sql.includes("INSERT INTO platform_audit_events") && state.auditFailure) throw new Error("Audit unavailable");
            return [{ affectedRows: 1 }];
        }),
    };
    const pool = { getConnection: vi.fn(async () => connection) };
    const evidenceStore = { verify: vi.fn(async () => {}) };
    const complete = () => completeValidationUpload(pool, evidenceStore, signed.upload.id, "worker-test", signed.report);
    return { ...signed, state, connection, pool, evidenceStore, complete };
}

describe("transactional validator completion", () => {
    it("verifies signed evidence before inserting and atomically updates upload/version/audit", async () => {
        const { complete, connection, evidenceStore, report } = fixture();
        const result = await complete();
        expect(result.reportId).toMatch(/^[0-9a-f-]{36}$/);
        expect(evidenceStore.verify).toHaveBeenCalledWith({ sha256: report.evidenceSha256, sizeBytes: report.evidenceSizeBytes });
        expect(connection.execute.mock.calls.filter(([sql]) => sql.startsWith("INSERT"))).toHaveLength(2);
        expect(connection.execute.mock.calls.some(([sql]) => sql.includes("SET state='validated'"))).toBe(true);
        expect(connection.commit).toHaveBeenCalledOnce();
    });

    it("rolls back a missing evidence result before any writes", async () => {
        const { complete, connection, evidenceStore } = fixture();
        evidenceStore.verify.mockRejectedValue(new Error("Missing file"));
        await expect(complete()).rejects.toThrow("evidence is missing");
        expect(connection.execute.mock.calls.some(([sql]) => /^(INSERT|UPDATE)/.test(sql))).toBe(false);
        expect(connection.rollback).toHaveBeenCalledOnce();
    });

    it("does not commit a lease that expires while evidence is verified", async () => {
        const { complete, state, connection } = fixture();
        state.live = 0;
        await expect(complete()).rejects.toThrow("lease was lost");
        expect(connection.execute.mock.calls.some(([sql]) => /^(INSERT|UPDATE)/.test(sql))).toBe(false);
    });

    it("rolls all completion changes back when audit storage fails", async () => {
        const { complete, state, connection } = fixture();
        state.auditFailure = true;
        await expect(complete()).rejects.toThrow("Audit unavailable");
        expect(connection.commit).not.toHaveBeenCalled();
        expect(connection.rollback).toHaveBeenCalledOnce();
    });

    it("returns a matching persisted receipt without reading evidence again", async () => {
        const { complete, state, connection, evidenceStore, upload } = fixture();
        state.receipt = { id: "recorded-report" };
        upload.state = "validated";
        expect(await complete()).toEqual({ reportId: "recorded-report" });
        expect(evidenceStore.verify).not.toHaveBeenCalled();
        expect(connection.execute.mock.calls.some(([sql]) => /^(INSERT|UPDATE)/.test(sql))).toBe(false);
    });

    it("rejects a forged signature before opening evidence", async () => {
        const { complete, report, evidenceStore } = fixture();
        report.attestation.document += " ";
        await expect(complete()).rejects.toThrow("signature is invalid");
        expect(evidenceStore.verify).not.toHaveBeenCalled();
    });

    it("stores a signed rejection without setting archive publication fields", async () => {
        const { complete, report, document, seal, connection } = fixture();
        Object.assign(report, { passed: false, malwareScanResult: "infected", diagnostics: [{ code: "MALWARE" }] });
        Object.assign(document, { passed: false, malwareScanResult: "infected" });
        seal();
        await complete();
        expect(connection.execute.mock.calls.some(([sql]) => sql.includes("SET state='validation_failed'"))).toBe(true);
        expect(connection.execute.mock.calls.some(([sql]) => sql.includes("archive_storage_bucket=?"))).toBe(false);
    });
});
