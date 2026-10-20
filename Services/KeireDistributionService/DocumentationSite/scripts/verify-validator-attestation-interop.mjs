import assert from "node:assert/strict";
import { verifyValidatorReport } from "../Source/lib/platform-validator-report.mjs";

let input = "";
for await (const chunk of process.stdin) {
    input += chunk.toString("utf8");
    if (Buffer.byteLength(input) > 1024 * 1024) {
        throw new Error("Fixture exceeds its size bound.");
    }
}
const { upload, report, key } = JSON.parse(input);
assert.equal(key.algorithm, "Ed25519");
const trusted = {
    key_id: key.keyId, algorithm: "ed25519", public_key_base64: key.publicKey,
    active: true, valid_from: "2020-01-01 00:00:00", valid_until: null,
};
const verified = await verifyValidatorReport(upload, report, trusted);
assert.equal(verified.uploadId, upload.id);
assert.equal(verified.passed, true);
await assert.rejects(verifyValidatorReport({ ...upload, storage_path: "wrong/package" }, report, trusted));
console.log("PASS: C# worker signature accepted by Node; wrong package identity rejected.");
