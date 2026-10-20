import { webcrypto } from "node:crypto";
import { ValidatorQueueError } from "./platform-validator-error.mjs";

const digestPattern = /^[0-9a-f]{64}$/;
const maximumEvidenceBytes = 8 * 1024 * 1024;

function reject(code, message) {
    throw new ValidatorQueueError(409, code, message);
}

function exactBase64(value, size) {
    if (typeof value !== "string") {
        reject("validator.attestation_invalid", "The validator signature is malformed.");
    }
    const bytes = Buffer.from(value, "base64");
    if (bytes.length !== size || bytes.toString("base64") !== value) {
        reject("validator.attestation_invalid", "The validator signature is malformed.");
    }
    return bytes;
}

function timestamp(value) {
    if (typeof value !== "string") {
        return NaN;
    }
    // Database strings have UTC semantics; signed worker timestamps must carry an explicit offset.
    const normalized = /^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d(?:\.\d+)?$/.test(value)
        ? `${value.replace(" ", "T")}Z` : value;
    return Date.parse(normalized);
}

function rejectDuplicateDocumentKeys(text) {
    const keys = new Set();
    for (let index = 0; index < text.length; index++) {
        if (text[index] !== '"') {
            continue;
        }
        const start = index++;
        while (index < text.length && text[index] !== '"') {
            if (text[index] === "\\") {
                index++;
            }
            index++;
        }
        const end = index + 1;
        let next = end;
        while (next < text.length && /\s/.test(text[next])) {
            next++;
        }
        if (text[next] !== ":") {
            continue;
        }
        const key = JSON.parse(text.slice(start, end));
        if (keys.has(key)) {
            reject("validator.attestation_invalid", "The signed validator document contains duplicate fields.");
        }
        keys.add(key);
    }
}

/** Pure verification. Caller must fetch/lock the trusted key and current lease, then verify evidence bytes. */
export async function verifyValidatorReport(upload, report, trustedKey, now = Date.now()) {
    if (!report || typeof report !== "object" || Array.isArray(report)
        || Buffer.byteLength(JSON.stringify(report), "utf8") > 1024 * 1024
        || report.schemaVersion !== 2 || typeof report.passed !== "boolean"
        || !digestPattern.test(report.packageSha256 ?? "")
        || report.packageSha256 !== upload.expected_sha256
        || !/^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$/.test(report.validatorVersion ?? "")
        || !/^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/.test(report.policyVersion ?? "")
        || !digestPattern.test(report.validatorFingerprintSha256 ?? "")
        || !["clean", "infected", "error", "not_applicable"].includes(report.malwareScanResult)
        || !["clean", "error", "not_applicable"].includes(report.secretScanResult)
        || !["passed", "error", "not_applicable"].includes(report.managedValidationResult)
        || !Array.isArray(report.diagnostics) || report.diagnostics.length > 1024
        || !digestPattern.test(report.evidenceSha256 ?? "")
        || !Number.isSafeInteger(report.evidenceSizeBytes) || report.evidenceSizeBytes < 2
        || report.evidenceSizeBytes > maximumEvidenceBytes
        || report.evidenceStoragePath !== `${upload.id}/review-evidence-${upload.expected_sha256}.json`
        || typeof report.completedAt !== "string"
        || !/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,7})?(?:Z|[+-]\d\d:\d\d)$/.test(report.completedAt)
        || !Number.isFinite(timestamp(report.completedAt))) {
        reject("validator.report_invalid", "The validator report does not match the uploaded package.");
    }
    for (const field of ["manifestSha256", "codeFingerprintSha256"]) {
        if (report[field] != null && !digestPattern.test(report[field])) {
            reject("validator.report_invalid", "The validator report contains an invalid digest.");
        }
    }
    if (report.passed && (report.malwareScanResult !== "clean" || report.secretScanResult !== "clean"
        || !["passed", "not_applicable"].includes(report.managedValidationResult)
        || report.diagnostics.length !== 0 || !report.manifestSha256)) {
        reject("validator.report_invalid", "The validator report cannot approve this package.");
    }
    const attestation = report.attestation;
    const signature = attestation?.signature;
    if (attestation?.schemaVersion !== 1 || typeof attestation.document !== "string"
        || Buffer.byteLength(attestation.document, "utf8") > 1024 * 1024
        || signature?.algorithm !== "ed25519" || typeof signature.keyId !== "string") {
        reject("validator.attestation_invalid", "The validator attestation is malformed.");
    }
    const validFrom = timestamp(trustedKey?.valid_from);
    const validUntil = trustedKey?.valid_until == null ? Infinity : timestamp(trustedKey.valid_until);
    if (!trustedKey || ![true, 1].includes(trustedKey.active) || trustedKey.algorithm !== "ed25519"
        || trustedKey.key_id !== signature.keyId || !Number.isFinite(validFrom)
        || validFrom > now || Number.isNaN(validUntil) || validUntil <= now) {
        reject("validator.attestation_key_untrusted", "The validator attestation key is not trusted.");
    }
    const key = await webcrypto.subtle.importKey("raw", exactBase64(trustedKey.public_key_base64, 32),
        { name: "Ed25519" }, false, ["verify"]);
    if (!await webcrypto.subtle.verify("Ed25519", key, exactBase64(signature.value, 64),
        Buffer.from(attestation.document, "utf8"))) {
        reject("validator.attestation_invalid", "The validator attestation signature is invalid.");
    }
    let document;
    try {
        document = JSON.parse(attestation.document);
    } catch {
        reject("validator.attestation_invalid", "The signed validator document is malformed.");
    }
    rejectDuplicateDocumentKeys(attestation.document);
    const expected = {
        schemaVersion: 1, keyId: signature.keyId, uploadId: upload.id, versionId: upload.version_id,
        storageBucket: upload.storage_bucket, storagePath: upload.storage_path,
        packageSha256: upload.expected_sha256, packageSizeBytes: Number(upload.expected_size_bytes),
        manifestSha256: report.manifestSha256 ?? null,
        evidenceStoragePath: report.evidenceStoragePath, evidenceSha256: report.evidenceSha256,
        evidenceSizeBytes: report.evidenceSizeBytes, validatorVersion: report.validatorVersion,
        validatorFingerprintSha256: report.validatorFingerprintSha256, policyVersion: report.policyVersion,
        malwareScanResult: report.malwareScanResult, secretScanResult: report.secretScanResult,
        managedValidationResult: report.managedValidationResult,
        codeFingerprintSha256: report.codeFingerprintSha256 ?? null,
        passed: report.passed, completedAt: report.completedAt,
    };
    if (!document || typeof document !== "object" || Array.isArray(document)
        || Object.keys(document).length !== Object.keys(expected).length
        || Object.entries(expected).some(([field, value]) => document[field] !== value)) {
        reject("validator.attestation_mismatch", "The signed validator report does not match its package and evidence.");
    }
    return document;
}
