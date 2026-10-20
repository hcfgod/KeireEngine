import { createHash, randomUUID } from "node:crypto";
import { platformTransaction } from "./platform-mysql.mjs";
import { ValidatorQueueError } from "./platform-validator-error.mjs";
import { verifyValidatorReport } from "./platform-validator-report.mjs";

function reportDigest(report) {
    function canonical(value, depth = 0) {
        if (depth > 24) {
            throw new ValidatorQueueError(400, "validator.report_invalid", "The validator report is too deeply nested.");
        }
        if (Array.isArray(value)) {
            return value.map((item) => canonical(item, depth + 1));
        }
        if (value !== null && typeof value === "object") {
            return Object.fromEntries(Object.keys(value).sort()
                .map((key) => [key, canonical(value[key], depth + 1)]));
        }
        return value;
    }
    const text = JSON.stringify(canonical(report));
    if (typeof text !== "string" || Buffer.byteLength(text) > 1024 * 1024) {
        throw new ValidatorQueueError(400, "validator.report_invalid", "The validator report is invalid or too large.");
    }
    return createHash("sha256").update(text).digest("hex");
}

function lostLease() {
    return new ValidatorQueueError(409, "validator.lease_lost", "The validation lease was lost.");
}

/** Internal broker operation: evidenceStore must be the host-owned immutable evidence store. */
export async function completeValidationUpload(pool, evidenceStore, uploadId, workerId, report) {
    if (typeof uploadId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(uploadId)
        || typeof workerId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(workerId)) {
        throw new ValidatorQueueError(400, "validator.request_invalid", "The validator completion identity is invalid.");
    }
    const digest = reportDigest(report);
    report = JSON.parse(JSON.stringify(report));
    return platformTransaction(pool, async (connection) => {
        const [[upload]] = await connection.execute(`SELECT upload.*,
            upload.lease_expires_at>UTC_TIMESTAMP(6) AS lease_live
            FROM marketplace_uploads upload WHERE upload.id=? FOR UPDATE`, [uploadId]);
        if (!upload) {
            throw lostLease();
        }
        const [[receipt]] = await connection.execute(`SELECT report.id FROM marketplace_validation_reports report
            JOIN platform_audit_events audit ON audit.target_id=report.upload_id
            AND audit.action='marketplace.validation.completed'
            WHERE report.upload_id=? AND JSON_UNQUOTE(JSON_EXTRACT(audit.metadata,'$.reportSha256'))=?
            AND JSON_UNQUOTE(JSON_EXTRACT(audit.metadata,'$.workerId'))=? LIMIT 1`,
        [uploadId, digest, workerId]);
        if (receipt) {
            return { reportId: receipt.id };
        }
        if (upload.state !== "leased" || upload.lease_owner !== workerId || !upload.lease_live) {
            throw lostLease();
        }
        const keyId = report?.attestation?.signature?.keyId;
        if (typeof keyId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(keyId)) {
            throw new ValidatorQueueError(409, "validator.attestation_invalid", "The validator key identifier is invalid.");
        }
        const [[key]] = await connection.execute(`SELECT * FROM marketplace_validator_attestation_keys
            WHERE key_id=? FOR SHARE`, [keyId]);
        await verifyValidatorReport(upload, report, key);
        try {
            await evidenceStore.verify({ sha256: report.evidenceSha256, sizeBytes: report.evidenceSizeBytes });
        } catch {
            throw new ValidatorQueueError(409, "validator.evidence_missing",
                "The signed review evidence is missing or does not match.");
        }
        // Verification can take time; an expired lease must not commit simply because its row stayed locked.
        const [[current]] = await connection.execute(`SELECT lease_expires_at>UTC_TIMESTAMP(6)
            AND EXISTS (SELECT 1 FROM marketplace_validator_attestation_keys
                WHERE key_id=? AND active=TRUE AND valid_from<=UTC_TIMESTAMP(6)
                AND (valid_until IS NULL OR valid_until>UTC_TIMESTAMP(6))) AS lease_live
            FROM marketplace_uploads WHERE id=?`, [keyId, uploadId]);
        if (!current.lease_live) {
            throw lostLease();
        }
        const reportId = randomUUID();
        const completedAt = new Date(report.completedAt).toISOString().replace("T", " ").replace("Z", "");
        await connection.execute(`INSERT INTO marketplace_validation_reports
            (id,upload_id,validator_version,validator_fingerprint_sha256,policy_version,package_sha256,
            manifest_sha256,passed,diagnostics,malware_scan_result,secret_scan_result,managed_validation_result,
            code_fingerprint,evidence_storage_path,evidence_sha256,evidence_size_bytes,attestation_key_id,
            signed_attestation,completed_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`,
        [reportId, uploadId, report.validatorVersion, report.validatorFingerprintSha256, report.policyVersion,
            report.packageSha256, report.manifestSha256 ?? null, report.passed, JSON.stringify(report.diagnostics),
            report.malwareScanResult, report.secretScanResult, report.managedValidationResult,
            report.codeFingerprintSha256 ?? null, report.evidenceStoragePath, report.evidenceSha256,
            report.evidenceSizeBytes, keyId, JSON.stringify(report.attestation), completedAt]);
        await connection.execute(`UPDATE marketplace_uploads SET state=?,lease_owner=NULL,lease_expires_at=NULL
            WHERE id=?`, [report.passed ? "validated" : "failed", uploadId]);
        if (report.passed) {
            await connection.execute(`UPDATE marketplace_product_versions SET state='validated',
                archive_storage_bucket=?,archive_storage_path=?,archive_sha256=?,archive_size_bytes=?,
                manifest_sha256=?,validator_version=?,executable_code_fingerprint=?,updated_at=UTC_TIMESTAMP(6)
                WHERE id=?`, [upload.storage_bucket, upload.storage_path, upload.expected_sha256,
                upload.expected_size_bytes, report.manifestSha256, report.validatorVersion,
                report.codeFingerprintSha256 ?? null, upload.version_id]);
        } else {
            await connection.execute(`UPDATE marketplace_product_versions SET state='validation_failed',
                updated_at=UTC_TIMESTAMP(6) WHERE id=?`, [upload.version_id]);
        }
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),NULL,NULL,'marketplace.validation.completed','marketplace_upload',?,?,?)`,
        [uploadId, randomUUID(), JSON.stringify({ workerId, passed: report.passed, reportId,
            validatorVersion: report.validatorVersion, policyVersion: report.policyVersion,
            attestationKeyId: keyId, evidenceSha256: report.evidenceSha256, reportSha256: digest })]);
        return { reportId };
    });
}
