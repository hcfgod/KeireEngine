import { canonicalJson } from "./platform-marketplace-import.mjs";

function rows(snapshot, name) {
    const value = snapshot.tables?.[name];
    if (!Array.isArray(value)) {
        throw new Error("Storage migration source table is missing.");
    }
    return value;
}

function objectKey(bucket, name) {
    if (typeof bucket !== "string" || !bucket || typeof name !== "string" || !name) {
        throw new Error("Invalid source storage identity.");
    }
    // Source names are opaque keys, never local filesystem paths.
    return JSON.stringify([bucket, name]);
}

export function planStorageRecovery(snapshot) {
    if (snapshot.project !== "khjduyjamzwumhducmou") {
        throw new Error("Unexpected storage source project.");
    }
    const buckets = new Map();
    for (const source of rows(snapshot, "storage.buckets")) {
        if (typeof source.id !== "string" || !source.id || buckets.has(source.id) ||
            typeof source.public !== "boolean") {
            throw new Error("Invalid or duplicate source bucket.");
        }
        if (source.type !== "STANDARD" || source.versioning_status !== "DISABLED") {
            throw new Error("Storage versioning or bucket type requires a reviewed migration.");
        }
        buckets.set(source.id, JSON.parse(canonicalJson(source)));
    }
    const objects = new Map();
    const ids = new Set();
    for (const source of rows(snapshot, "storage.objects")) {
        const key = objectKey(source.bucket_id, source.name);
        if (!buckets.has(source.bucket_id) || objects.has(key) ||
            typeof source.id !== "string" || !source.id || ids.has(source.id)) {
            throw new Error("Duplicate or orphan source storage object.");
        }
        if (source.is_versioned !== false || source.is_delete_marker !== false || source.archived_at !== null) {
            throw new Error("Historical storage versions require a reviewed migration.");
        }
        const sizeBytes = source.metadata?.size;
        if (!Number.isSafeInteger(sizeBytes) || sizeBytes < 0 ||
            (source.metadata.contentLength !== undefined && source.metadata.contentLength !== sizeBytes)) {
            throw new Error("Source storage size is invalid or inconsistent.");
        }
        ids.add(source.id);
        objects.set(key, {
            source: JSON.parse(canonicalJson(source)),
            sizeBytes,
            expectedSha256: null,
            references: [],
            recoveryStatus: "missing",
        });
    }
    const missingReferences = [];
    function reference(table, row, bucket, name, hash, size) {
        if (name === null) {
            return;
        }
        const key = objectKey(bucket, name);
        if (typeof hash !== "string" || !/^[a-f0-9]{64}$/.test(hash) ||
            (size !== null && (!Number.isSafeInteger(size) || size < 0))) {
            throw new Error("Invalid source asset integrity reference.");
        }
        const object = objects.get(key);
        const identity = { table, id: row.id };
        if (!object) {
            missingReferences.push({ ...identity, bucket, name });
            return;
        }
        if ((size !== null && size !== object.sizeBytes) ||
            (object.expectedSha256 !== null && object.expectedSha256 !== hash)) {
            throw new Error("Conflicting source asset integrity references.");
        }
        object.expectedSha256 = hash;
        object.references.push(identity);
    }
    for (const row of rows(snapshot, "public.marketplace_uploads")) {
        reference("marketplace_uploads", row, row.storage_bucket, row.storage_path,
            row.expected_sha256, row.expected_size_bytes);
    }
    for (const row of rows(snapshot, "public.marketplace_product_versions")) {
        reference("marketplace_product_versions", row, row.archive_storage_bucket, row.archive_storage_path,
            row.archive_sha256, row.archive_size_bytes);
    }
    for (const row of rows(snapshot, "public.marketplace_validation_reports")) {
        reference("marketplace_validation_reports", row, "marketplace-validation-evidence", row.evidence_storage_path,
            row.evidence_sha256, row.evidence_size_bytes);
    }
    // Media bucket routing needs separate review before using this plan as a cutover gate.
    const unreviewedMediaReferences = rows(snapshot, "public.marketplace_product_media").length;
    const entries = [...objects.values()];
    return {
        buckets: [...buckets.values()],
        objects: entries,
        missingReferences,
        unreviewedMediaReferences,
        summary: {
            buckets: buckets.size,
            objects: entries.length,
            withExpectedHash: entries.filter((entry) => entry.expectedSha256 !== null).length,
            withoutExpectedHash: entries.filter((entry) => entry.expectedSha256 === null).length,
            missingReferencedObjects: missingReferences.length,
            unreviewedMediaReferences,
            recoveredObjects: 0,
        },
    };
}
