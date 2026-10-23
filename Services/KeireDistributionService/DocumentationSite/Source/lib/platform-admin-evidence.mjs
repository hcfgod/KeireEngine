import { createHash } from "node:crypto";
import { verifyValidatorReport } from "./platform-validator-report.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

const maximumEvidenceBytes = 8 * 1024 * 1024;
const reportIdPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

function fail(status, code, message) {
    throw new LocalUploadError(status, code, message);
}

function milliseconds(value) {
    if (value instanceof Date) {
        return value.getTime();
    }
    return Date.parse(typeof value === "string" && /^\d{4}-\d\d-\d\d /.test(value)
        ? value.replace(" ", "T") + "Z" : value);
}

async function verifiedReport(connection, reportId) {
    const [[saved]] = await connection.execute(
        "SELECT * FROM marketplace_validation_reports WHERE id=? FOR SHARE", [reportId]);
    if (!saved || typeof saved.signed_attestation !== "string"
        || Buffer.byteLength(saved.signed_attestation) > 1024 * 1024) {
        fail(409, "evidence.unavailable", "This report has no signed review evidence.");
    }
    const [[upload]] = await connection.execute(
        "SELECT id,version_id,storage_bucket,storage_path,expected_sha256,expected_size_bytes FROM marketplace_uploads WHERE id=? FOR SHARE",
        [saved.upload_id]);
    const [[key]] = await connection.execute(
        "SELECT key_id,algorithm,public_key_base64,fingerprint,active,valid_from,valid_until FROM marketplace_validator_attestation_keys WHERE key_id=? FOR SHARE",
        [saved.attestation_key_id]);
    try {
        const attestation = JSON.parse(saved.signed_attestation);
        const document = JSON.parse(attestation.document);
        // Preserve signed sub-millisecond precision while checking the persisted UTC timestamp at DB precision.
        if (!Number.isFinite(milliseconds(saved.completed_at))
            || milliseconds(saved.completed_at) !== milliseconds(document.completedAt)) {
            throw new Error("Completion timestamp mismatch.");
        }
        const report = {
            schemaVersion: 2, passed: [true, 1].includes(saved.passed), diagnostics: saved.diagnostics,
            packageSha256: saved.package_sha256, manifestSha256: saved.manifest_sha256,
            validatorVersion: saved.validator_version, validatorFingerprintSha256: saved.validator_fingerprint_sha256,
            policyVersion: saved.policy_version, malwareScanResult: saved.malware_scan_result,
            secretScanResult: saved.secret_scan_result, managedValidationResult: saved.managed_validation_result,
            codeFingerprintSha256: saved.code_fingerprint, completedAt: document.completedAt,
            evidenceStoragePath: saved.evidence_storage_path, evidenceSha256: saved.evidence_sha256,
            evidenceSizeBytes: Number(saved.evidence_size_bytes), attestation,
        };
        await verifyValidatorReport(upload, report, key);
        const fingerprint = `sha256:${createHash("sha256").update(Buffer.from(key.public_key_base64, "base64")).digest("hex")}`;
        if (fingerprint !== key.fingerprint) {
            throw new Error("Key fingerprint mismatch.");
        }
        return { identity: { sha256: report.evidenceSha256, sizeBytes: report.evidenceSizeBytes },
            fingerprint, keyId: key.key_id };
    } catch {
        fail(409, "evidence.attestation_invalid", "The signed evidence or its trusted validator key could not be verified.");
    }
}

export async function readLocalAdminEvidence(authentication, evidenceStore, token, reportId) {
    if (typeof reportId !== "string" || !reportIdPattern.test(reportId)) {
        fail(400, "evidence.report_invalid", "Choose a valid validation report.");
    }
    const result = await authentication.withSessionTransaction(token, async (connection, session) => {
        if (session.assurance !== 2) {
            fail(403, "account.mfa_required", "Verify your authenticator before reading private evidence.");
        }
        const [[staff]] = await connection.execute(
            "SELECT role,active,revoked_at FROM platform_staff_members WHERE user_id=? FOR SHARE", [session.userId]);
        if (!staff || ![true, 1].includes(staff.active) || staff.revoked_at !== null
            || !["moderator", "administrator"].includes(staff.role)) {
            fail(403, "staff.authorization_required", "An active staff role is required.");
        }
        if (typeof evidenceStore?.read !== "function") {
            fail(503, "evidence.storage_unavailable", "Private evidence storage is not configured yet.");
        }
        const verified = await verifiedReport(connection, reportId);
        const chunks = [];
        let size = 0;
        const hash = createHash("sha256");
        try {
            for await (const chunk of evidenceStore.read(verified.identity)) {
                if (!(chunk instanceof Uint8Array) || chunk.byteLength > maximumEvidenceBytes - size
                    || chunk.byteLength > verified.identity.sizeBytes - size) {
                    throw new Error("Evidence exceeds its signed bounds.");
                }
                const owned = Buffer.from(chunk);
                chunks.push(owned);
                hash.update(owned);
                size += owned.length;
            }
            if (size !== verified.identity.sizeBytes || hash.digest("hex") !== verified.identity.sha256) {
                throw new Error("Evidence digest mismatch.");
            }
        } catch {
            fail(503, "evidence.storage_unavailable", "The verified evidence bytes are unavailable. Retry after storage recovery.");
        }
        return { reportId, ...verified, bytes: Buffer.concat(chunks, size) };
    });
    if (!result) {
        fail(401, "account.authentication_required", "Sign in again to read private evidence.");
    }
    return result;
}
