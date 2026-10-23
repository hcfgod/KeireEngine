import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { mkdtempSync, writeFileSync, unlinkSync, rmdirSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { expect, it } from "vitest";
import { createValidatorReportFixture } from "./validator-report-fixture.mjs";

const verifier = fileURLToPath(new URL("../../tests/KeireMarketplaceValidator.Tests/VerifyOfflineWorkerReport.mjs", import.meta.url));

it.each(["valid", "valid-managed", "evidence-tampered", "worker-mismatch", "wrong-outcome", "managed-not-run"])(
    "checks the offline report verifier subprocess: %s", (scenario) => {
        const root = mkdtempSync(path.join(os.tmpdir(), "keire-offline-verifier-"));
        const files = ["report.json", "evidence.json", "public.json"].map((name) => path.join(root, name));
        try {
            const fixture = createValidatorReportFixture();
            const evidence = Buffer.from(JSON.stringify({ schemaVersion: 1 }));
            const evidenceFields = {
                evidenceSha256: createHash("sha256").update(evidence).digest("hex"),
                evidenceSizeBytes: evidence.length,
            };
            Object.assign(fixture.report, evidenceFields);
            Object.assign(fixture.document, evidenceFields);
            if (scenario === "valid-managed") {
                const managedFields = { managedValidationResult: "passed", codeFingerprintSha256: "e".repeat(64) };
                Object.assign(fixture.report, managedFields);
                Object.assign(fixture.document, managedFields);
            }
            fixture.seal();
            writeFileSync(files[0], JSON.stringify(fixture.report));
            writeFileSync(files[1], scenario === "evidence-tampered" ? Buffer.from('{"schemaVersion":2}') : evidence);
            writeFileSync(files[2], JSON.stringify({ algorithm: "Ed25519", keyId: fixture.trustedKey.key_id,
                publicKey: fixture.trustedKey.public_key_base64 }));
            const configuration = {
                report: files[0], evidence: files[1], publicKey: files[2], upload: fixture.upload,
                expectedOutcome: scenario === "wrong-outcome" ? "blocked" : "clean",
                requireManagedCompilation: ["managed-not-run", "valid-managed"].includes(scenario),
                workerSha256: scenario === "worker-mismatch" ? "0".repeat(64) : fixture.report.validatorFingerprintSha256,
            };
            const result = spawnSync(process.execPath, [verifier], {
                input: JSON.stringify(configuration), encoding: "utf8", timeout: 10000, windowsHide: true,
            });
            expect(result.error).toBeUndefined();
            const expectedSuccess = ["valid", "valid-managed"].includes(scenario);
            expect(result.status === 0).toBe(expectedSuccess);
            if (expectedSuccess) {
                expect(JSON.parse(result.stdout).passed).toBe(true);
            }
        } finally {
            for (const file of files) {
                try {
                    unlinkSync(file);
                } catch (error) {
                    if (error.code !== "ENOENT") {
                        throw error;
                    }
                }
            }
            rmdirSync(root);
        }
    },
);
