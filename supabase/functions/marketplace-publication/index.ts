import {
    authenticate, databaseFailure, handleError, json, preflight, readJson, RequestError, requiredUuid,
    stringField, validateOrigin,
} from "../_shared/marketplace.ts";

interface PublicationDocument {
    schemaVersion: number;
    keyId: string;
    sequence: number;
    expiresAt: string;
    productId: string;
    versionId: string;
    artifactSha256: string;
    artifactSizeBytes: number;
    manifestSha256: string;
    releaseStoragePath: string;
}

interface SignedPublicationEnvelope {
    schemaVersion: number;
    document: string;
    signature: {
        algorithm: string;
        keyId: string;
        value: string;
        sequence: number;
        expiresAt: string;
    };
}

function exactObject(value: unknown, keys: readonly string[]): value is Record<string, unknown> {
    return Boolean(value && typeof value === "object" && !Array.isArray(value) &&
        Object.keys(value as Record<string, unknown>).sort().join("\0") === [...keys].sort().join("\0"));
}

function decodeBase64(value: string, expectedBytes: number, field: string): Uint8Array {
    if (value.length > 4096 || !/^[A-Za-z0-9+/]+={0,2}$/.test(value)) {
        throw new RequestError(400, "publication.signature_invalid", `${field} is not canonical base64.`);
    }
    try {
        const decoded = Uint8Array.from(atob(value), (character) => character.charCodeAt(0));
        if (decoded.byteLength !== expectedBytes || btoa(String.fromCharCode(...decoded)) !== value) {
            throw new Error("noncanonical");
        }
        return decoded;
    } catch {
        throw new RequestError(400, "publication.signature_invalid", `${field} is not canonical base64.`);
    }
}

function parseEnvelope(value: string): { envelope: SignedPublicationEnvelope; document: PublicationDocument } {
    let envelopeValue: unknown;
    let documentValue: unknown;
    try {
        envelopeValue = JSON.parse(value);
        if (!exactObject(envelopeValue, ["schemaVersion", "document", "signature"])) throw new Error("envelope");
        const envelope = envelopeValue as Record<string, unknown>;
        if (typeof envelope.document !== "string" || envelope.document.length > 64 * 1024) throw new Error("document");
        documentValue = JSON.parse(envelope.document);
    } catch {
        throw new RequestError(400, "publication.manifest_invalid", "The signed publication envelope is malformed.");
    }
    const envelope = envelopeValue as unknown as SignedPublicationEnvelope;
    if (envelope.schemaVersion !== 1 || !exactObject(envelope.signature,
        ["algorithm", "keyId", "value", "sequence", "expiresAt"]) ||
        !exactObject(documentValue, ["schemaVersion", "keyId", "sequence", "expiresAt", "productId", "versionId",
            "artifactSha256", "artifactSizeBytes", "manifestSha256", "releaseStoragePath"])) {
        throw new RequestError(400, "publication.manifest_invalid", "The signed publication schema is unsupported.");
    }
    const document = documentValue as unknown as PublicationDocument;
    if (document.schemaVersion !== 1 || !Number.isSafeInteger(document.sequence) || document.sequence < 1 ||
        !Number.isSafeInteger(document.artifactSizeBytes) || document.artifactSizeBytes < 1 ||
        !/^[0-9a-f]{64}$/.test(document.artifactSha256) || !/^[0-9a-f]{64}$/.test(document.manifestSha256) ||
        !/^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(document.keyId) ||
        !/^[0-9a-f-]{36}\/[0-9a-f-]{36}\/[0-9a-f]{64}\.keireassetpackage$/.test(document.releaseStoragePath) ||
        !Number.isFinite(Date.parse(document.expiresAt)) || Date.parse(document.expiresAt) <= Date.now() ||
        envelope.signature.algorithm !== "ed25519" || envelope.signature.keyId !== document.keyId ||
        envelope.signature.sequence !== document.sequence || envelope.signature.expiresAt !== document.expiresAt) {
        throw new RequestError(400, "publication.manifest_invalid", "The signed publication fields are invalid.");
    }
    decodeBase64(envelope.signature.value, 64, "signature.value");
    return { envelope, document };
}

async function administrator(caller: Awaited<ReturnType<typeof authenticate>>): Promise<void> {
    const { data, error } = await caller.admin.rpc("service_get_platform_staff_role", {
        p_actor_user_id: caller.user.id,
    });
    if (error) throw databaseFailure(error);
    if (data !== "administrator") {
        throw new RequestError(403, "staff.administrator_required", "An active administrator role is required.");
    }
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
        await administrator(caller);
        const input = await readJson(request);
        const versionId = requiredUuid(input, "versionId");
        const signedManifest = stringField(input, "signedManifest", 2, 64 * 1024);
        const { envelope, document } = parseEnvelope(signedManifest);
        if (document.versionId !== versionId) {
            throw new RequestError(409, "publication.version_mismatch", "The signature targets a different package version.");
        }

        const { data: existing } = await caller.admin.from("marketplace_publications")
            .select("id,version_id,published_at,signed_manifest").eq("version_id", versionId).maybeSingle();
        if (existing) {
            if (existing.signed_manifest !== signedManifest) {
                throw new RequestError(409, "publication.already_exists", "This immutable version already has a different publication record.");
            }
            return json(origin, 200, { data: { publicationId: existing.id, versionId, publishedAt: existing.published_at } });
        }

        const [{ data: key, error: keyError }, { data: version, error: versionError }] = await Promise.all([
            caller.admin.from("marketplace_signature_keys")
                .select("key_id,algorithm,public_key_base64,active,valid_from,valid_until")
                .eq("key_id", document.keyId).eq("active", true).maybeSingle(),
            caller.admin.from("marketplace_product_versions")
                .select("id,product_id,state,archive_sha256,archive_size_bytes,manifest_sha256")
                .eq("id", versionId).maybeSingle(),
        ]);
        if (keyError || !key || key.algorithm !== "ed25519" || Date.parse(key.valid_from) > Date.now() ||
            (key.valid_until && Date.parse(key.valid_until) <= Date.now())) {
            throw new RequestError(409, "publication.key_untrusted", "The marketplace signing key is not trusted.");
        }
        if (versionError || !version || version.state !== "approved_pending_signature") {
            throw new RequestError(409, "publication.not_ready", "This version is not approved for offline signing.");
        }
        if (document.productId !== version.product_id || document.artifactSha256 !== version.archive_sha256 ||
            document.artifactSizeBytes !== Number(version.archive_size_bytes) ||
            document.manifestSha256 !== version.manifest_sha256 ||
            document.releaseStoragePath !== `${version.product_id}/${version.id}/${version.archive_sha256}.keireassetpackage`) {
            throw new RequestError(409, "publication.evidence_mismatch", "The signature does not match the approved validator evidence.");
        }

        const publicKeyBytes = decodeBase64(key.public_key_base64, 32, "publicKey");
        const signatureBytes = decodeBase64(envelope.signature.value, 64, "signature.value");
        const verificationKey = await crypto.subtle.importKey("raw", publicKeyBytes, { name: "Ed25519" }, false, ["verify"]);
        if (!await crypto.subtle.verify("Ed25519", verificationKey, signatureBytes,
            new TextEncoder().encode(envelope.document))) {
            throw new RequestError(409, "publication.signature_invalid", "The offline marketplace signature is invalid.");
        }

        const { data: submission, error: submissionError } = await caller.admin.from("marketplace_submissions")
            .select("validation_report_id,state").eq("version_id", versionId)
            .eq("state", "approved_pending_signature").order("decided_at", { ascending: false }).limit(1).maybeSingle();
        if (submissionError || !submission) throw new RequestError(409, "publication.not_ready", "No approved submission is available.");
        const { data: validation, error: validationError } = await caller.admin.from("marketplace_validation_reports")
            .select("upload_id,passed,package_sha256,manifest_sha256").eq("id", submission.validation_report_id).maybeSingle();
        if (validationError || !validation?.passed || validation.package_sha256 !== document.artifactSha256 ||
            validation.manifest_sha256 !== document.manifestSha256) {
            throw new RequestError(409, "publication.evidence_mismatch", "The passing validation report no longer matches the signature.");
        }
        const { data: upload, error: uploadError } = await caller.admin.from("marketplace_uploads")
            .select("storage_path,state").eq("id", validation.upload_id).maybeSingle();
        if (uploadError || !upload || upload.state !== "validated") {
            throw new RequestError(409, "publication.source_unavailable", "The exact validated quarantine object is unavailable.");
        }

        const copy = await caller.admin.storage.from("marketplace-quarantine")
            .copy(upload.storage_path, document.releaseStoragePath, { destinationBucket: "marketplace-releases" });
        if (copy.error) {
            throw new RequestError(503, "publication.promotion_failed", "The validated package could not be promoted to immutable release storage.");
        }
        const { data: publication, error: publicationError } = await caller.admin.rpc("service_publish_marketplace_version", {
            p_actor_user_id: caller.user.id,
            p_version_id: versionId,
            p_release_storage_path: document.releaseStoragePath,
            p_artifact_sha256: document.artifactSha256,
            p_manifest_sha256: document.manifestSha256,
            p_signature_key_id: document.keyId,
            p_signed_manifest: signedManifest,
        });
        if (publicationError) {
            await caller.admin.storage.from("marketplace-releases").remove([document.releaseStoragePath]);
            throw databaseFailure(publicationError);
        }
        const result = Array.isArray(publication) ? publication[0] : publication;
        return json(origin, 201, { data: result });
    } catch (error) {
        return handleError(origin, error);
    }
});

