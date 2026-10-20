import { generateKeyPairSync, sign } from "node:crypto";

export function createValidatorReportFixture(uploadOverrides = {}) {
    const { privateKey, publicKey } = generateKeyPairSync("ed25519");
    const upload = {
        id: "11111111-1111-4111-8111-111111111111", version_id: "22222222-2222-4222-8222-222222222222",
        storage_bucket: "marketplace-quarantine", storage_path: "product/version/package.keireassetpackage",
        expected_size_bytes: "1024", expected_sha256: "a".repeat(64),
        ...uploadOverrides,
    };
    const trustedKey = {
        key_id: "test-worker-key", algorithm: "ed25519", active: true,
        public_key_base64: Buffer.from(publicKey.export({ format: "jwk" }).x, "base64url").toString("base64"),
        valid_from: "2020-01-01 00:00:00.000000", valid_until: null,
    };
    const report = {
        schemaVersion: 2, packageSha256: upload.expected_sha256, manifestSha256: "b".repeat(64),
        passed: true, diagnostics: [], validatorVersion: "0.4.5", policyVersion: "marketplace-2026-08-13",
        validatorFingerprintSha256: "c".repeat(64), malwareScanResult: "clean", secretScanResult: "clean",
        managedValidationResult: "not_applicable", evidenceSha256: "d".repeat(64), evidenceSizeBytes: 128,
        evidenceStoragePath: `${upload.id}/review-evidence-${upload.expected_sha256}.json`,
        completedAt: "2026-10-20T12:00:00+00:00",
    };
    const document = {
        schemaVersion: 1, keyId: trustedKey.key_id, uploadId: upload.id, versionId: upload.version_id,
        storageBucket: upload.storage_bucket, storagePath: upload.storage_path,
        packageSha256: upload.expected_sha256, packageSizeBytes: Number(upload.expected_size_bytes), manifestSha256: report.manifestSha256,
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

