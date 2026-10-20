import { execFile } from "node:child_process";
import path from "node:path";
import { promisify } from "node:util";
import { loadNativeSettings } from "./platform-native-settings.mjs";

const execute = promisify(execFile);

/** Host bootstrap only. Child output and errors must never be logged. */
export async function loadWindowsNativeSecrets({ powerShellExecutable, decryptScriptPath, settingsDirectory,
    siteOrigin, clientId }) {
    let settings;
    try {
        for (const value of [powerShellExecutable, decryptScriptPath, settingsDirectory]) {
            if (typeof value !== "string" || !path.isAbsolute(value)) {
                throw new Error();
            }
        }
        const result = await execute(powerShellExecutable, [
            "-NoLogo", "-NoProfile", "-NonInteractive", "-File", decryptScriptPath,
            "-SettingsDirectory", settingsDirectory,
        ], { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 8192, encoding: "utf8" });
        settings = JSON.parse(result.stdout.replace(/^\uFEFF/, ""));
        return loadNativeSettings(settings, siteOrigin, clientId);
    } catch {
        throw new Error("Protected native identity settings are unavailable.");
    } finally {
        if (settings) {
            settings.privateKeyPkcs8 = null;
        }
    }
}
