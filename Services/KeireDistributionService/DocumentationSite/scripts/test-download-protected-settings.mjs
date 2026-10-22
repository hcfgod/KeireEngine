import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadWindowsDownloadSecrets } from "../Source/lib/platform-download-secrets.mjs";
import { createDownloadCapabilities } from "../Source/lib/platform-download-capabilities.mjs";

const execute = promisify(execFile);
const scripts = fileURLToPath(new URL("../../scripts/", import.meta.url));
const settingsDirectory = path.join(process.env.TEMP, `keire-download-qa-${randomUUID()}`);
const powerShellExecutable = process.env.KEIRE_QA_POWERSHELL;
const siteOrigin = "https://download-qa.invalid";
const options = { shell: false, windowsHide: true, timeout: 30000, maxBuffer: 8192 };
const args = ["-NoProfile", "-NonInteractive", "-File", path.join(scripts, "configure-local-platform-downloads.ps1"),
    "-SettingsDirectory", settingsDirectory, "-SiteOrigin", siteOrigin];
let first;
let second;
let capabilities;
try {
    const configured = JSON.parse((await execute(powerShellExecutable, args, options)).stdout);
    assert.equal(configured.status, "download-key-protected");
    const encrypted = await readFile(path.join(settingsDirectory, "download-settings.dpapi"));
    const loader = { powerShellExecutable, settingsDirectory, siteOrigin,
        decryptScriptPath: path.join(scripts, "read-local-platform-downloads.ps1") };
    first = await loadWindowsDownloadSecrets(loader);
    second = await loadWindowsDownloadSecrets(loader);
    assert.deepEqual(first, second);
    capabilities = createDownloadCapabilities({ siteOrigin, key: first });
    first.fill(0);
    const grantId = randomUUID();
    const url = capabilities.issue(grantId, new Date(Date.now() + 60000).toISOString());
    assert.equal(capabilities.verify(url).grantId, grantId);
    await assert.rejects(execute(powerShellExecutable, args, options));
    assert.deepEqual(await readFile(path.join(settingsDirectory, "download-settings.dpapi")), encrypted);
    await assert.rejects(loadWindowsDownloadSecrets({ ...loader, siteOrigin: "https://wrong.invalid" }),
        { message: "Protected download settings are unavailable." });
    console.log(JSON.stringify({ status: "download-protected-settings-passed", settingsDirectory,
        stableKey: true, replacementRejected: true, hostMismatchRejected: true }));
} catch {
    console.error(JSON.stringify({ status: "failed", settingsDirectory }));
    process.exitCode = 1;
} finally {
    first?.fill(0);
    second?.fill(0);
    capabilities?.close();
}
