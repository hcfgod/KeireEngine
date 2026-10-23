import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadProductionWorkerSecrets } from "./platform-production-worker-secrets.mjs";

const execute = promisify(execFile);
const root = path.dirname(fileURLToPath(import.meta.url));
const settingsDirectory = path.join(os.tmpdir(), `keire-worker-qa-${randomUUID()}`);
const setup = path.resolve(root, "../../scripts/configure-local-platform-workers.ps1");
const powerShellExecutable = process.env.KEIRE_QA_POWERSHELL;
assert.ok(powerShellExecutable && path.isAbsolute(powerShellExecutable));
const argumentsList = ["-NoProfile", "-NonInteractive", "-File", setup,
    "-SettingsDirectory", settingsDirectory, "-SiteOrigin", "https://worker-qa.invalid"];
const options = { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 8192 };
await execute(powerShellExecutable, argumentsList, options);
const encryptedPath = path.join(settingsDirectory, "worker-settings.dpapi");
const encrypted = await readFile(encryptedPath);
const configuration = { powerShellExecutable, settingsDirectory, siteOrigin: "https://worker-qa.invalid",
    decryptScriptPath: path.resolve(root, "../../scripts/read-local-platform-workers.ps1") };
const first = await loadProductionWorkerSecrets(configuration);
const second = await loadProductionWorkerSecrets(configuration);
try {
    assert.equal(first.brokerSecret, second.brokerSecret);
    assert.equal(first.publicationSecret, second.publicationSecret);
    assert.deepEqual(first.transferKey, second.transferKey);
    assert.notEqual(first.brokerSecret, first.publicationSecret);
    assert.equal(encrypted.includes(Buffer.from(first.brokerSecret)), false);
    assert.equal(encrypted.includes(Buffer.from(first.publicationSecret)), false);
    assert.equal(encrypted.includes(first.transferKey), false);
    await assert.rejects(loadProductionWorkerSecrets({ ...configuration, siteOrigin: "https://other.invalid" }));
    await assert.rejects(execute(powerShellExecutable, argumentsList, options));
    assert.deepEqual(await readFile(encryptedPath), encrypted);
} finally {
    first.transferKey.fill(0);
    second.transferKey.fill(0);
    first.brokerSecret = first.publicationSecret = null;
    second.brokerSecret = second.publicationSecret = null;
}
console.log(JSON.stringify({ status: "passed", checks: 10, fixtureDirectory: settingsDirectory,
    productionChanged: false }));
