import {
    authenticate, databaseFailure, handleError, json, preflight, readJson, RequestError, requiredUuid,
    stringField, validateOrigin,
} from "../_shared/marketplace.ts";

function booleanField(input: Record<string, unknown>, name: string): boolean {
    if (typeof input[name] !== "boolean") {
        throw new RequestError(400, "request.invalid_field", `${name} must be a boolean.`);
    }
    return input[name] as boolean;
}

async function staffRole(caller: Awaited<ReturnType<typeof authenticate>>): Promise<string> {
    const { data, error } = await caller.admin.rpc("service_get_platform_staff_role", {
        p_actor_user_id: caller.user.id,
    });
    if (error) throw databaseFailure(error);
    if (data !== "moderator" && data !== "administrator") {
        throw new RequestError(403, "staff.authorization_required", "An active staff role is required.");
    }
    return data;
}

function requireAdministrator(role: string): void {
    if (role !== "administrator") {
        throw new RequestError(403, "staff.administrator_required", "An active administrator role is required.");
    }
}

function canonicalBase64(value: unknown, expectedBytes: number, label: string): ArrayBuffer {
    if (typeof value !== "string" || value.length > 4096 || /\s/.test(value)) {
        throw new RequestError(409, "evidence.attestation_invalid", `${label} is malformed.`);
    }
    let decoded: Uint8Array;
    try {
        decoded = Uint8Array.from(atob(value), (character) => character.charCodeAt(0));
    } catch {
        throw new RequestError(409, "evidence.attestation_invalid", `${label} is malformed.`);
    }
    let roundTrip = "";
    for (let offset = 0; offset < decoded.length; offset += 0x8000) {
        roundTrip += String.fromCharCode(...decoded.subarray(offset, offset + 0x8000));
    }
    if (decoded.length !== expectedBytes || btoa(roundTrip) !== value) {
        throw new RequestError(409, "evidence.attestation_invalid", `${label} is malformed.`);
    }
    const result = new ArrayBuffer(decoded.byteLength);
    new Uint8Array(result).set(decoded);
    return result;
}

async function issueEvidence(
    caller: Awaited<ReturnType<typeof authenticate>>,
    reportId: string,
): Promise<Record<string, unknown>> {
    const { data: report, error: reportError } = await caller.admin.from("marketplace_validation_reports")
        .select("id,passed,malware_scan_result,secret_scan_result,managed_validation_result,validator_version,validator_fingerprint_sha256,policy_version,package_sha256,manifest_sha256,evidence_storage_path,evidence_sha256,evidence_size_bytes,attestation_key_id,signed_attestation")
        .eq("id", reportId).maybeSingle();
    if (reportError) throw databaseFailure(reportError);
    if (!report?.evidence_storage_path || !report.evidence_sha256 || !report.evidence_size_bytes ||
        !report.attestation_key_id || !report.signed_attestation) {
        throw new RequestError(409, "evidence.unavailable", "This report has no signed review evidence.");
    }

    const { data: key, error: keyError } = await caller.admin.from("marketplace_validator_attestation_keys")
        .select("key_id,algorithm,public_key_base64,fingerprint")
        .eq("key_id", report.attestation_key_id).maybeSingle();
    if (keyError) throw databaseFailure(keyError);
    if (!key || key.algorithm !== "ed25519") {
        throw new RequestError(409, "evidence.attestation_key_missing", "The validator attestation key is unavailable.");
    }

    let attestation: Record<string, unknown>;
    let document: Record<string, unknown>;
    try {
        const parsedAttestation: unknown = JSON.parse(report.signed_attestation);
        if (!parsedAttestation || typeof parsedAttestation !== "object" || Array.isArray(parsedAttestation)) {
            throw new Error("attestation");
        }
        attestation = parsedAttestation as Record<string, unknown>;
        if (typeof attestation.document !== "string") throw new Error("document text");
        const parsedDocument: unknown = JSON.parse(attestation.document);
        if (!parsedDocument || typeof parsedDocument !== "object" || Array.isArray(parsedDocument)) {
            throw new Error("document");
        }
        document = parsedDocument as Record<string, unknown>;
    } catch {
        throw new RequestError(409, "evidence.attestation_invalid", "The validator attestation is malformed.");
    }
    const signatureValue = attestation.signature;
    if (!signatureValue || typeof signatureValue !== "object" || Array.isArray(signatureValue)) {
        throw new RequestError(409, "evidence.attestation_invalid", "The validator signature is malformed.");
    }
    const signatureDocument = signatureValue as Record<string, unknown>;
    const publicKey = canonicalBase64(key.public_key_base64, 32, "The validator public key");
    const signature = canonicalBase64(signatureDocument.value, 64, "The validator signature");
    if (attestation.schemaVersion !== 1 || signatureDocument.algorithm !== "ed25519" ||
        signatureDocument.keyId !== key.key_id || document.schemaVersion !== 1 ||
        document.keyId !== key.key_id || document.packageSha256 !== report.package_sha256 ||
        document.manifestSha256 !== report.manifest_sha256 ||
        document.evidenceStoragePath !== report.evidence_storage_path ||
        document.evidenceSha256 !== report.evidence_sha256 ||
        document.evidenceSizeBytes !== report.evidence_size_bytes || document.passed !== report.passed ||
        document.malwareScanResult !== report.malware_scan_result ||
        document.secretScanResult !== report.secret_scan_result ||
        document.managedValidationResult !== report.managed_validation_result ||
        document.validatorVersion !== report.validator_version ||
        document.validatorFingerprintSha256 !== report.validator_fingerprint_sha256 ||
        document.policyVersion !== report.policy_version) {
        throw new RequestError(409, "evidence.attestation_invalid", "The validator attestation does not match this report.");
    }
    const imported = await crypto.subtle.importKey("raw", publicKey, { name: "Ed25519" }, false, ["verify"]);
    if (!await crypto.subtle.verify(
        { name: "Ed25519" }, imported, signature, new TextEncoder().encode(attestation.document as string)
    )) {
        throw new RequestError(409, "evidence.attestation_invalid", "The validator attestation signature is invalid.");
    }

    const { data: signed, error: signedError } = await caller.admin.storage
        .from("marketplace-validation-evidence").createSignedUrl(report.evidence_storage_path, 300, {
            download: `marketplace-review-${report.id}.json`,
        });
    if (signedError || !signed?.signedUrl) {
        throw new RequestError(503, "evidence.signing_failed", "The verified review evidence is temporarily unavailable.");
    }
    return {
        reportId: report.id,
        url: signed.signedUrl,
        sha256: report.evidence_sha256,
        sizeBytes: report.evidence_size_bytes,
        expiresInSeconds: 300,
        attestationKeyId: key.key_id,
        attestationFingerprint: key.fingerprint,
    };
}

Deno.serve(async (request: Request) => {
    let origin = "";
    try {
        origin = validateOrigin(request);
        if (request.method === "OPTIONS") return preflight(origin);
        if (request.method !== "POST") throw new RequestError(405, "request.method_not_allowed", "POST is required.");
        const caller = await authenticate(request);
        if (caller.assuranceLevel !== "aal2") {
            throw new RequestError(403, "account.mfa_required", "A verified second factor is required.");
        }
        const role = await staffRole(caller);
        const input = await readJson(request);
        const operation = stringField(input, "operation", 3, 64);

        if (operation === "application.decide") {
            const { data, error } = await caller.admin.rpc("service_decide_publisher_application", {
                p_actor_user_id: caller.user.id,
                p_application_id: requiredUuid(input, "applicationId"),
                p_decision: stringField(input, "decision", 8, 32),
                p_decision_note: stringField(input, "decisionNote", 3, 5000),
                p_publisher_slug: typeof input.publisherSlug === "string" ? input.publisherSlug : "",
            });
            if (error) throw databaseFailure(error);
            const decision = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: decision });
        }
        if (operation === "submission.decide") {
            const { data, error } = await caller.admin.rpc("service_decide_marketplace_submission", {
                p_actor_user_id: caller.user.id,
                p_submission_id: requiredUuid(input, "submissionId"),
                p_decision: stringField(input, "decision", 8, 32),
                p_decision_note: typeof input.decisionNote === "string" ? input.decisionNote : "",
            });
            if (error) throw databaseFailure(error);
            const decision = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: decision });
        }
        if (operation === "evidence.issue") {
            return json(origin, 200, { data: await issueEvidence(caller, requiredUuid(input, "reportId")) });
        }
        if (operation === "report.decide") {
            const { data, error } = await caller.admin.rpc("service_decide_marketplace_report", {
                p_actor_user_id: caller.user.id,
                p_report_id: requiredUuid(input, "reportId"),
                p_state: stringField(input, "state", 7, 16),
                p_resolution_note: typeof input.resolutionNote === "string" ? input.resolutionNote : "",
            });
            if (error) throw databaseFailure(error);
            const decision = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: decision });
        }
        if (operation === "staff.set") {
            requireAdministrator(role);
            const { data, error } = await caller.admin.rpc("service_set_platform_staff", {
                p_actor_user_id: caller.user.id,
                p_target_user_id: requiredUuid(input, "targetUserId"),
                p_role: stringField(input, "role", 9, 16),
                p_active: booleanField(input, "active"),
                p_reason: stringField(input, "reason", 3, 1000),
            });
            if (error) throw databaseFailure(error);
            const assignment = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: assignment });
        }
        if (operation === "feature.set") {
            requireAdministrator(role);
            const { data, error } = await caller.admin.rpc("service_set_platform_feature_flag", {
                p_actor_user_id: caller.user.id,
                p_key: stringField(input, "key", 3, 64),
                p_enabled: booleanField(input, "enabled"),
                p_reason: stringField(input, "reason", 3, 1000),
            });
            if (error) throw databaseFailure(error);
            const flag = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: flag });
        }
        throw new RequestError(400, "request.operation_invalid", "The moderation operation is invalid.");
    } catch (error) {
        return handleError(origin, error);
    }
});
