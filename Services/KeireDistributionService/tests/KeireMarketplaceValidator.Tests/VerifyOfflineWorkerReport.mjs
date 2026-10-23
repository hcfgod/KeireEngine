import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { lstat, readFile } from "node:fs/promises";
import { verifyValidatorReport } from "../../DocumentationSite/Source/lib/platform-validator-report.mjs";

let input = "";
for await (const chunk of process.stdin) {
    input += chunk.toString("utf8");
    assert.ok(Buffer.byteLength(input) <= 65536, "Worker verification configuration exceeded its bound.");
}
const configuration = JSON.parse(input);
async function boundedFile(path, maximumBytes) {
    const information = await lstat(path);
    assert.ok(information.isFile() && !information.isSymbolicLink() && information.size <= maximumBytes);
    const bytes = await readFile(path);
    assert.ok(bytes.length <= maximumBytes);
    return bytes;
}
const report = JSON.parse(await boundedFile(configuration.report, 1024 * 1024));
const evidenceBytes = await boundedFile(configuration.evidence, 8 * 1024 * 1024);
const evidence = JSON.parse(evidenceBytes);
const key = JSON.parse(await boundedFile(configuration.publicKey, 16384));
assert.equal(key.algorithm, "Ed25519");
const trusted = { key_id: key.keyId, algorithm: "ed25519", public_key_base64: key.publicKey,
    active: true, valid_from: "2020-01-01 00:00:00", valid_until: null };
await verifyValidatorReport(configuration.upload, report, trusted);
assert.equal(report.validatorFingerprintSha256, configuration.workerSha256);
assert.equal(report.evidenceSizeBytes, evidenceBytes.length);
assert.equal(report.evidenceSha256, createHash("sha256").update(evidenceBytes).digest("hex"));
assert.equal(evidence.schemaVersion, 1);
assert.equal(report.passed, configuration.expectedOutcome === "clean");
if (configuration.expectedOutcome === "clean") {
    assert.equal(report.malwareScanResult, "clean");
    assert.equal(report.secretScanResult, "clean");
    assert.ok(["passed", "not_applicable"].includes(report.managedValidationResult));
    if (configuration.requireManagedCompilation === true) {
        assert.equal(report.managedValidationResult, "passed");
        assert.match(report.codeFingerprintSha256, /^[0-9a-f]{64}$/);
    }
} else {
    assert.equal(report.malwareScanResult, "infected");
    assert.ok(report.diagnostics.some((diagnostic) => diagnostic.code === "MALWARE_DETECTED"));
}
console.log(JSON.stringify({ passed: true, actualWorkerProcess: true, expectedOutcome: configuration.expectedOutcome,
    malwareScanResult: report.malwareScanResult, managedValidationResult: report.managedValidationResult,
    packageSha256: report.packageSha256, validatorFingerprintSha256: report.validatorFingerprintSha256,
    evidenceSha256: report.evidenceSha256 }));
