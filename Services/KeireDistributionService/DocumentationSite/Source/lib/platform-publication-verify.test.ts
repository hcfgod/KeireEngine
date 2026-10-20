import { generateKeyPairSync, sign } from "node:crypto";
import { describe, expect, it } from "vitest";
import { verifyPublicationEnvelope } from "./platform-publication-verify.mjs";

function encode(value) {
    return JSON.stringify(value, (_, item) => typeof item === "bigint" ? `INTEGER:${item}` : item)
        .replace(/"INTEGER:(-?\d+)"/g, "$1");
}

function fixture() {
    const keys = generateKeyPairSync("ed25519");
    const now = Date.parse("2026-10-20T12:00:00Z");
    const release = {
        signing_key_id: "test-publication",
        publication_sequence: "9007199254740993",
        publication_expires_at: "2027-10-20 12:00:00.123456",
        product_id: "11111111-1111-4111-8111-111111111111",
        version_id: "22222222-2222-4222-8222-222222222222",
        artifact_sha256: "a".repeat(64), manifest_sha256: "b".repeat(64),
        artifact_size_bytes: "12345", storage_path: "publisher/package.keireasset",
    };
    const key = {
        key_id: release.signing_key_id, active: true, algorithm: "ed25519",
        valid_from: "2026-01-01 00:00:00", valid_until: null,
        public_key_base64: Buffer.from(keys.publicKey.export({ format: "jwk" }).x!, "base64url")
            .toString("base64"),
    };
    const document = {
        schemaVersion: 1n, keyId: key.key_id, sequence: BigInt(release.publication_sequence),
        expiresAt: "2027-10-20T12:00:00.1234560Z",
        productId: release.product_id, versionId: release.version_id,
        artifactSha256: release.artifact_sha256, artifactSizeBytes: 12345n,
        manifestSha256: release.manifest_sha256, releaseStoragePath: release.storage_path,
    };
    function envelope(documentText = encode(document)) {
        return {
            schemaVersion: 1n, document: documentText,
            signature: {
                algorithm: "ed25519", keyId: key.key_id,
                value: sign(null, Buffer.from(documentText), keys.privateKey).toString("base64"),
                sequence: document.sequence, expiresAt: document.expiresAt,
            },
        };
    }
    return { now, release, key, document, envelope };
}

describe("signed publication verification", () => {
    it("verifies exact bytes and a sequence beyond JavaScript's safe integer range", async () => {
        const value = fixture();
        const text = `\r\n ${encode(value.document)} \r\n`;
        const result = await verifyPublicationEnvelope(encode(value.envelope(text)),
            value.release, value.key, value.now);
        expect(result.document.sequence).toBe(9007199254740993n);
        expect(result.documentText).toBe(text);
    });

    it.each(["productId", "versionId", "artifactSha256", "manifestSha256", "releaseStoragePath"])(
        "rejects a valid signature for a different %s", async (field) => {
            const value = fixture();
            value.document[field] = "different";
            await expect(verifyPublicationEnvelope(encode(value.envelope()),
                value.release, value.key, value.now)).rejects.toMatchObject({ code: "publication.signature_invalid" });
        },
    );

    it.each(["sequence", "artifactSizeBytes"])("rejects a signed integer mismatch for %s", async (field) => {
        const value = fixture();
        value.document[field] -= 1n;
        await expect(verifyPublicationEnvelope(encode(value.envelope()),
            value.release, value.key, value.now)).rejects.toThrow();
    });

    it("rejects a one-tick expiry mismatch that Date.parse would round away", async () => {
        const value = fixture();
        value.document.expiresAt = "2027-10-20T12:00:00.1234561Z";
        await expect(verifyPublicationEnvelope(encode(value.envelope()),
            value.release, value.key, value.now)).rejects.toThrow();
    });

    it.each(["active", "key_id", "public_key_base64", "valid_from", "valid_until"])(
        "rejects an untrusted key: %s", async (field) => {
            const value = fixture();
            const invalid = {
                active: false, key_id: "other", public_key_base64: Buffer.alloc(32).toString("base64"),
                valid_from: "2027-01-01 00:00:00", valid_until: "2026-10-20 12:00:00",
            };
            value.key[field] = invalid[field];
            await expect(verifyPublicationEnvelope(encode(value.envelope()),
                value.release, value.key, value.now)).rejects.toThrow();
        },
    );

    it("rejects altered signed bytes", async () => {
        const value = fixture();
        const envelope = value.envelope();
        envelope.document += " ";
        await expect(verifyPublicationEnvelope(encode(envelope),
            value.release, value.key, value.now)).rejects.toThrow();
    });

    it("rejects a signed duplicate field", async () => {
        const value = fixture();
        const text = encode(value.document).replace('"schemaVersion":1', '"schemaVersion":1,"schemaVersion":1');
        await expect(verifyPublicationEnvelope(encode(value.envelope(text)),
            value.release, value.key, value.now)).rejects.toThrow();
    });

    it("rejects an extra signed field", async () => {
        const value = fixture();
        const text = encode({ ...value.document, extra: 1 });
        await expect(verifyPublicationEnvelope(encode(value.envelope(text)),
            value.release, value.key, value.now)).rejects.toThrow();
    });

    it("rejects expired publications", async () => {
        const value = fixture();
        await expect(verifyPublicationEnvelope(encode(value.envelope()),
            value.release, value.key, Date.parse("2028-01-01T00:00:00Z"))).rejects.toThrow();
    });
});
