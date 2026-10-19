import { execFile } from "node:child_process";
import path from "node:path";
import { promisify } from "node:util";

const execute = promisify(execFile);

/** Call only from server bootstrap. Never forward the child output or error object to logs. */
export async function loadWindowsPlatformSecrets({ powerShellExecutable, decryptScriptPath, settingsDirectory }) {
    for (const value of [powerShellExecutable, decryptScriptPath, settingsDirectory]) {
        if (typeof value !== "string" || !path.isAbsolute(value)) {
            throw new Error("Protected platform loader paths must be absolute.");
        }
    }
    let output;
    let key;
    try {
        const result = await execute(powerShellExecutable, [
            "-NoLogo", "-NoProfile", "-NonInteractive", "-File", decryptScriptPath,
            "-SettingsDirectory", settingsDirectory,
        ], {
            shell: false,
            windowsHide: true,
            timeout: 15000,
            maxBuffer: 32768,
            encoding: "utf8",
        });
        output = JSON.parse(result.stdout.replace(/^\uFEFF/, ""));
        if (!output?.settings || Object.hasOwn(output.settings, "migration") ||
            typeof output.accountEncryptionKey !== "string" ||
            !/^[A-Za-z0-9+/]{43}=$/.test(output.accountEncryptionKey)) {
            throw new Error("Invalid protected runtime payload.");
        }
        key = Buffer.from(output.accountEncryptionKey, "base64");
        if (key.length !== 32 || key.toString("base64") !== output.accountEncryptionKey) {
            throw new Error("Invalid protected runtime key.");
        }
        return { settings: output.settings, accountEncryptionKey: key };
    } catch {
        key?.fill(0);
        // Child-process errors may include stdout. Do not expose their message, stack, or cause.
        throw new Error("Protected platform runtime settings are unavailable.");
    } finally {
        if (output) {
            output.accountEncryptionKey = null;
        }
    }
}
