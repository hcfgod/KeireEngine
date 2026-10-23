import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadWindowsGitHubSecrets } from "../Source/lib/platform-github-secrets.mjs";
import { windowsGitHubServiceFactory } from "../Source/lib/platform-github-factory.mjs";

const execute = promisify(execFile);
const directory = path.dirname(fileURLToPath(import.meta.url));
const settingsDirectory = path.join(os.tmpdir(), `keire-github-qa-${randomUUID()}`);
const script = path.resolve(directory, "../../scripts/configure-local-platform-github.ps1");
const reader = path.resolve(directory, "../../scripts/read-local-platform-github.ps1");
const powerShellExecutable = process.env.KEIRE_QA_POWERSHELL;
const quote = (value) => "'" + value.replaceAll("'", "''") + "'";
if (!powerShellExecutable || !path.isAbsolute(powerShellExecutable)) {
    throw new Error("Set KEIRE_QA_POWERSHELL to an absolute PowerShell executable.");
}
const command = `& ${quote(script)} -SettingsDirectory ${quote(settingsDirectory)} -SiteOrigin 'https://github-qa.invalid' -ClientId 'Iv1.syntheticfixture' -ClientSecret (ConvertTo-SecureString 'synthetic_qa_value_not_a_real_credential' -AsPlainText -Force)`;
const options = { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 8192, encoding: "utf8" };
await execute(powerShellExecutable, ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", command], options);
const encryptedPath = path.join(settingsDirectory, "github-settings.dpapi");
const encrypted = await readFile(encryptedPath);
assert.equal(encrypted.includes(Buffer.from("synthetic_qa_value_not_a_real_credential")), false);
const configuration = { powerShellExecutable, decryptScriptPath: reader, settingsDirectory, siteOrigin: "https://github-qa.invalid" };
const loaded = await loadWindowsGitHubSecrets(configuration);
assert.equal(loaded.clientId, "Iv1.syntheticfixture");
assert.equal(loaded.clientSecret.toString(), "synthetic_qa_value_not_a_real_credential");
loaded.clientSecret.fill(0);
await assert.rejects(loadWindowsGitHubSecrets({ ...configuration, siteOrigin: "https://other.invalid" }), /unavailable/);
await assert.rejects(execute(powerShellExecutable, ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", command], options));
assert.deepEqual(await readFile(encryptedPath), encrypted);
const service = await windowsGitHubServiceFactory(configuration)({ authentication: {} });
assert.equal(await service.handle(new Request("https://github-qa.invalid/unrelated"), "loopback"), null);
await service.close();
console.log(JSON.stringify({ status: "passed", checks: 7, fixtureDirectory: settingsDirectory,
    note: "Only synthetic encrypted fixture retained; no production credentials changed." }));
