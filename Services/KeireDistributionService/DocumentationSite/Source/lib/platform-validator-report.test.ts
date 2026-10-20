import { createValidatorReportFixture as fixture } from "../../scripts/validator-report-fixture.mjs";
import { describe, expect, it } from "vitest";
import { verifyValidatorReport } from "./platform-validator-report.mjs";

describe("validator report attestation", () => {
    it("verifies actual Ed25519 signatures and every signed package field", async () => {
        const { upload, report, trustedKey, document } = fixture();
        expect(await verifyValidatorReport(upload, report, trustedKey)).toEqual(document);
    });

    it.each(["uploadId", "versionId", "storageBucket", "storagePath", "packageSha256",
        "manifestSha256", "evidenceStoragePath", "evidenceSha256", "validatorVersion",
        "validatorFingerprintSha256", "policyVersion", "malwareScanResult", "secretScanResult",
        "managedValidationResult", "codeFingerprintSha256", "completedAt"])(
        "rejects a valid signature over the wrong %s", async (field) => {
            const { upload, report, trustedKey, document, seal } = fixture();
            document[field] = "different";
            seal();
            await expect(verifyValidatorReport(upload, report, trustedKey)).rejects.toThrow("does not match");
        });

    it("rejects byte tampering after signing", async () => {
        const { upload, report, trustedKey } = fixture();
        report.attestation.document += " ";
        await expect(verifyValidatorReport(upload, report, trustedKey)).rejects.toThrow("signature is invalid");
    });

    it.each([
        { active: false }, { algorithm: "rsa" }, { key_id: "another-key" },
        { valid_from: "2099-01-01 00:00:00" }, { valid_until: "2000-01-01 00:00:00" },
        { valid_from: "invalid" },
    ])("rejects an untrusted or inactive key", async (change) => {
        const { upload, report, trustedKey } = fixture();
        await expect(verifyValidatorReport(upload, report, { ...trustedKey, ...change })).rejects.toThrow("not trusted");
    });

    it.each([
        { malwareScanResult: "infected" }, { secretScanResult: "error" }, { managedValidationResult: "error" },
        { diagnostics: [{ code: "REJECTED" }] }, { manifestSha256: null },
    ])("rejects contradictory success reports", async (change) => {
        const { upload, report, trustedKey } = fixture();
        Object.assign(report, change);
        await expect(verifyValidatorReport(upload, report, trustedKey)).rejects.toThrow("cannot approve");
    });

    it("rejects duplicate signed fields even when the signature is authentic", async () => {
        const { upload, report, trustedKey, document, seal } = fixture();
        seal(JSON.stringify(document).replace('"schemaVersion":1', '"schemaVersion":1,"schemaVersion":1'));
        await expect(verifyValidatorReport(upload, report, trustedKey)).rejects.toThrow("duplicate");
    });

    it("accepts a signed failure without granting success or requiring a manifest", async () => {
        const { upload, report, trustedKey, document, seal } = fixture();
        Object.assign(report, { passed: false, manifestSha256: null, malwareScanResult: "infected", diagnostics: [{ code: "MALWARE" }] });
        Object.assign(document, { passed: false, manifestSha256: null, malwareScanResult: "infected" });
        seal();
        expect((await verifyValidatorReport(upload, report, trustedKey)).passed).toBe(false);
    });
});
