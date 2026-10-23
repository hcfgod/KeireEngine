import { execFile } from "node:child_process";
import path from "node:path";
import { promisify } from "node:util";
import { loadGitHubSettings } from "./platform-github-settings.mjs";

const execute = promisify(execFile);

/** Host bootstrap only: child stdout is a private pipe and must never be logged. */
export async function loadWindowsGitHubSecrets({ powerShellExecutable, decryptScriptPath, settingsDirectory, siteOrigin }) {
    let settings;
    try {
        for (const value of [powerShellExecutable, decryptScriptPath, settingsDirectory]) {
            if (typeof value !== "string" || !path.isAbsolute(value)) {
                throw new Error();
            }
        }
        const result = await execute(powerShellExecutable, ["-NoLogo", "-NoProfile", "-NonInteractive", "-File",
            decryptScriptPath, "-SettingsDirectory", settingsDirectory],
        { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 8192, encoding: "utf8" });
        settings = JSON.parse(result.stdout.replace(/^\uFEFF/, ""));
        return loadGitHubSettings(settings, siteOrigin);
    } catch {
        throw new Error("Protected GitHub settings are unavailable.");
    } finally {
        if (settings) {
            settings.clientSecret = null;
        }
    }
}
