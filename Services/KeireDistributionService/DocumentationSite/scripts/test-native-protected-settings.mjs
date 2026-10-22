import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { randomUUID } from "node:crypto";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { loadWindowsNativeSecrets } from "../Source/lib/platform-native-secrets.mjs";
import { createNativeIdentitySigner } from "../Source/lib/platform-native-identity.mjs";
import { windowsNativeServiceFactory } from "../Source/lib/platform-native-factory.mjs";

const execute = promisify(execFile);
const scripts = fileURLToPath(new URL("../../scripts/", import.meta.url));
const settingsDirectory = path.join(process.env.TEMP, `keire-native-qa-${randomUUID()}`);
const powerShellExecutable = process.env.KEIRE_QA_POWERSHELL;
const siteOrigin = "https://native-qa.invalid";
const clientId = "keire-hub-desktop";
const options = { shell: false, windowsHide: true, timeout: 30000, maxBuffer: 8192 };
const args = ["-NoProfile", "-NonInteractive", "-File", path.join(scripts, "configure-local-platform-native.ps1"),
    "-SettingsDirectory", settingsDirectory, "-SiteOrigin", siteOrigin, "-ClientId", clientId];
try {
    const configured = JSON.parse((await execute(powerShellExecutable, args, options)).stdout);
    const encrypted = await readFile(path.join(settingsDirectory, "native-settings.dpapi"));
    const loader = { powerShellExecutable, settingsDirectory, siteOrigin, clientId,
        decryptScriptPath: path.join(scripts, "read-local-platform-native.ps1") };
    for (let attempt = 0; attempt < 2; attempt++) {
        const signer = createNativeIdentitySigner(await loadWindowsNativeSecrets(loader));
        assert.equal(signer.jwks.keys[0].kid, configured.keyId);
        signer.close();
    }
    await assert.rejects(execute(powerShellExecutable, args, options));
    assert.deepEqual(await readFile(path.join(settingsDirectory, "native-settings.dpapi")), encrypted);
    await assert.rejects(loadWindowsNativeSecrets({ ...loader, siteOrigin: "https://wrong.invalid" }),
        { message: "Protected native identity settings are unavailable." });
    const downloadDirectory = `${settingsDirectory}-downloads`;
    await execute(powerShellExecutable, ["-NoProfile", "-NonInteractive", "-File",
        path.join(scripts, "configure-local-platform-downloads.ps1"), "-SettingsDirectory", downloadDirectory,
        "-SiteOrigin", siteOrigin], options);
    const native = await windowsNativeServiceFactory({ ...loader, downloads: {
        settingsDirectory: downloadDirectory, decryptScriptPath: path.join(scripts, "read-local-platform-downloads.ps1"),
        packageStore: {
            read() { throw new Error("Invalid capabilities must not read storage."); },
            verify() { throw new Error("Discovery must not read storage."); },
        },
    } })({
        pool: { getConnection() { throw new Error("Discovery must not access the database."); } },
        authentication: { session() {}, withSessionTransaction() {} },
    });
    try {
        const response = await native.handle(new Request(`${siteOrigin}/auth/v1/.well-known/jwks.json`));
        assert.equal(response.status, 200);
        const publicDocument = await response.json();
        assert.equal(publicDocument.keys[0].kid, configured.keyId);
        assert.equal(Object.hasOwn(publicDocument.keys[0], "d"), false);
        const download = await native.handle(new Request(`${siteOrigin}/marketplace/v1/downloads/invalid`));
        assert.equal(download.status, 403);
    } finally {
        await native.close();
    }
    console.log(JSON.stringify({ status: "native-protected-settings-passed", settingsDirectory,
        stableIdentity: true, replacementRejected: true, hostMismatchRejected: true }));
} catch {
    console.error(JSON.stringify({ status: "failed", settingsDirectory }));
    process.exitCode = 1;
}
