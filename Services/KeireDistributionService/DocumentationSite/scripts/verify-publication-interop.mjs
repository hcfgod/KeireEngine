import assert from "node:assert/strict";
import { verifyPublicationEnvelope } from "../Source/lib/platform-publication-verify.mjs";

let input = "";
for await (const chunk of process.stdin) {
    input += chunk.toString("utf8");
    if (Buffer.byteLength(input) > 1024 * 1024) {
        throw new Error("Publication fixture exceeds its size bound.");
    }
}
const { signedManifest, release, key } = JSON.parse(input);
assert.equal(key.algorithm, "Ed25519");
const trusted = {
    key_id: key.keyId,
    algorithm: "ed25519",
    public_key_base64: key.publicKey,
    active: true,
    valid_from: "2020-01-01 00:00:00",
    valid_until: null,
};
const verified = await verifyPublicationEnvelope(signedManifest, release, trusted);
assert.equal(verified.document.sequence, BigInt(release.publication_sequence));
assert.equal(verified.document.versionId, release.version_id);
await assert.rejects(verifyPublicationEnvelope(signedManifest,
    { ...release, publication_sequence: (BigInt(release.publication_sequence) - 1n).toString() }, trusted));
await assert.rejects(verifyPublicationEnvelope(signedManifest,
    { ...release, storage_path: "different/package" }, trusted));
await assert.rejects(verifyPublicationEnvelope(signedManifest, release, { ...trusted, active: false }));
console.log("PASS: Actual C# publication signature accepted by Node; sequence, path and revoked-key mismatches rejected.");
