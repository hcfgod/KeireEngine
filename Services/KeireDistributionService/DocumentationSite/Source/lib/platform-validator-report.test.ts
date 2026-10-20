import { generateKeyPairSync, sign } from "node:crypto";
import { describe, expect, it } from "vitest";
import { verifyValidatorReport } from "./platform-validator-report.mjs";

function fixture() {
    const { privateKey, publicKey } = generateKeyPairSync("ed25519");
    const upload = {
        id: "11111111-1111-4111-8111-111111111111", version_id: "22222222-2222-4222-8222-222222222222",
        storage_bucket: "marketplace-quarantine", storage_path: "product/version/package.keireassetpackage",
        expected_size_bytes: "1024", expected_sha256: "a".repeat(64),
    };
    const trustedKey = {
        key_id: "test-worker-key", algorithm: "ed25519", active: true,
        public_key_base64: Buffer.from(publicKey.export({ format: "jwk" }).x!, "base64url").toString("base64"),
        valid_from: "2020-01-01 00:00:00.000000", valid_until: null,
    };
    const report: any = {
        schemaVersion: 2, packageSha256: upload.expected_sha256, manifestSha256: "b".repeat(64),
        passed: true, diagnostics: [], validatorVersion: "0.4.5", policyVersion: "marketplace-2026-08-13",
        validatorFingerprintSha256: "c".repeat(64), malwareScanResult: "clean", secretScanResult: "clean",
        managedValidationResult: "not_applicable", evidenceSha256: "d".repeat(64), evidenceSizeBytes: 128,
        evidenceStoragePath: `${upload.id}/review-evidence-${upload.expected_sha256}.json`,
        completedAt: "2026-10-20T12:00:00+00:00",
    };
    const document: any = {
        schemaVersion: 1, keyId: trustedKey.key_id, uploadId: upload.id, versionId: upload.version_id,
        storageBucket: upload.storage_bucket, storagePath: upload.storage_path,
        packageSha256: upload.expected_sha256, packageSizeBytes: 1024, manifestSha256: report.manifestSha256,
        evidenceStoragePath: report.evidenceStoragePath, evidenceSha256: report.evidenceSha256,
        evidenceSizeBytes: report.evidenceSizeBytes, validatorVersion: report.validatorVersion,
        validatorFingerprintSha256: report.validatorFingerprintSha256, policyVersion: report.policyVersion,
        malwareScanResult: report.malwareScanResult, secretScanResult: report.secretScanResult,
        managedValidationResult: report.managedValidationResult, codeFingerprintSha256: null,
        passed: report.passed, completedAt: report.completedAt,
    };
    const seal = (text = JSON.stringify(document, null, 2)) => {
        report.attestation = { schemaVersion: 1, document: text, signature: {
            algorithm: "ed25519", keyId: trustedKey.key_id, value: sign(null, Buffer.from(text), privateKey).toString("base64"),
        } };
    };
    seal();
    return { upload, report, trustedKey, document, seal };
}

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
