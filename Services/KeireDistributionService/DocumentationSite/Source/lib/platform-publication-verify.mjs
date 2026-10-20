import { webcrypto } from "node:crypto";
import { parsePublicationJson } from "./platform-publication-json.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

function reject() {
    throw new LocalUploadError(409, "publication.signature_invalid",
        "The signed publication does not match the approved release or its trusted signing key.");
}

function exactFields(value, fields) {
    return value !== null && typeof value === "object"
        && Object.keys(value).length === fields.length
        && fields.every((field) => Object.hasOwn(value, field));
}

function exactBase64(value, size) {
    if (typeof value !== "string") {
        return reject();
    }
    const bytes = Buffer.from(value, "base64");
    if (bytes.length !== size || bytes.toString("base64") !== value) {
        return reject();
    }
    return bytes;
}

function positiveInteger(value, maximum) {
    if (typeof value !== "bigint" && (typeof value !== "string" || !/^[1-9][0-9]*$/.test(value))) {
        return reject();
    }
    const integer = BigInt(value);
    if (integer < 1n || integer > maximum) {
        return reject();
    }
    return integer;
}

// Compare database microseconds and C# seven-digit UTC timestamps without rounding to milliseconds.
function utcTicks(value, database = false) {
    if (value instanceof Date && database) {
        value = value.toISOString();
    }
    if (database && typeof value === "string" && value.includes(" ")) {
        value = `${value.replace(" ", "T")}Z`;
    }
    const match = typeof value === "string"
        ? /^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(?:\.(\d{1,7}))?Z$/.exec(value) : null;
    if (!match) {
        return reject();
    }
    const milliseconds = Date.parse(`${match[1]}Z`);
    if (!Number.isFinite(milliseconds) || new Date(milliseconds).toISOString().slice(0, 19) !== match[1]) {
        return reject();
    }
    return BigInt(milliseconds) * 10000n + BigInt((match[2] ?? "").padEnd(7, "0"));
}

/** Caller owns the transaction, current approval/lease locks, and trusted key lookup. */
export async function verifyPublicationEnvelope(text, release, trustedKey, now = Date.now()) {
    let envelope;
    let document;
    try {
        envelope = parsePublicationJson(text);
        document = parsePublicationJson(envelope.document);
    } catch {
        return reject();
    }
    const signature = envelope.signature;
    if (!exactFields(envelope, ["schemaVersion", "document", "signature"]) || envelope.schemaVersion !== 1n
        || !exactFields(signature, ["algorithm", "keyId", "value", "sequence", "expiresAt"])
        || signature.algorithm !== "ed25519" || typeof signature.keyId !== "string"
        || !Number.isSafeInteger(now)) {
        return reject();
    }
    const currentTicks = BigInt(now) * 10000n;
    if (!trustedKey || ![true, 1].includes(trustedKey.active) || trustedKey.algorithm !== "ed25519"
        || trustedKey.key_id !== signature.keyId || release.signing_key_id !== signature.keyId
        || utcTicks(trustedKey.valid_from, true) > currentTicks
        || (trustedKey.valid_until != null && utcTicks(trustedKey.valid_until, true) <= currentTicks)) {
        return reject();
    }
    const sequence = positiveInteger(release.publication_sequence, 9223372036854775807n);
    const size = positiveInteger(release.artifact_size_bytes, 64n * 1024n * 1024n * 1024n);
    const expected = {
        schemaVersion: 1n,
        keyId: signature.keyId,
        sequence,
        expiresAt: signature.expiresAt,
        productId: release.product_id,
        versionId: release.version_id,
        artifactSha256: release.artifact_sha256,
        artifactSizeBytes: size,
        manifestSha256: release.manifest_sha256,
        releaseStoragePath: release.storage_path,
    };
    if (!exactFields(document, Object.keys(expected))
        || Object.entries(expected).some(([field, value]) => document[field] !== value)
        || signature.sequence !== sequence
        || utcTicks(signature.expiresAt) !== utcTicks(release.publication_expires_at, true)
        || utcTicks(signature.expiresAt) <= currentTicks) {
        return reject();
    }
    try {
        const key = await webcrypto.subtle.importKey("raw", exactBase64(trustedKey.public_key_base64, 32),
            { name: "Ed25519" }, false, ["verify"]);
        const valid = await webcrypto.subtle.verify("Ed25519", key, exactBase64(signature.value, 64),
            Buffer.from(envelope.document, "utf8"));
        if (!valid) {
            return reject();
        }
    } catch {
        return reject();
    }
    return { document, documentText: envelope.document };
}
