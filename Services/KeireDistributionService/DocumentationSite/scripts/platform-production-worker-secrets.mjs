import { execFile } from "node:child_process";
import path from "node:path";
import { promisify } from "node:util";

const execute = promisify(execFile);

/** Protected worker settings are supplied through a private decryptor pipe, never process arguments or environment. */
export async function loadProductionWorkerSecrets({ powerShellExecutable, decryptScriptPath, settingsDirectory, siteOrigin }) {
    let data;
    let transferKey;
    try {
        if (![powerShellExecutable, decryptScriptPath, settingsDirectory].every((value) => typeof value === "string" && path.isAbsolute(value))) {
            throw new Error();
        }
        const result = await execute(powerShellExecutable, ["-NoLogo", "-NoProfile", "-NonInteractive", "-File",
            decryptScriptPath, "-SettingsDirectory", settingsDirectory],
        { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 8192, encoding: "utf8" });
        data = JSON.parse(result.stdout.replace(/^\uFEFF/, ""));
        if (Object.keys(data).sort().join(",") !== "brokerSecret,publicationSecret,schemaVersion,siteOrigin,transferKey" ||
            data.schemaVersion !== 1 || data.siteOrigin !== siteOrigin ||
            typeof data.brokerSecret !== "string" || !/^[A-Za-z0-9_-]{43,128}$/.test(data.brokerSecret) ||
            typeof data.publicationSecret !== "string" || !/^[A-Za-z0-9_-]{43,128}$/.test(data.publicationSecret) ||
            data.brokerSecret === data.publicationSecret || typeof data.transferKey !== "string") {
            throw new Error();
        }
        transferKey = Buffer.from(data.transferKey, "base64");
        if (transferKey.length !== 32 || transferKey.toString("base64") !== data.transferKey) {
            throw new Error();
        }
        return { brokerSecret: data.brokerSecret, publicationSecret: data.publicationSecret, transferKey };
    } catch {
        transferKey?.fill(0);
        throw new Error("Protected production worker credentials are unavailable.");
    } finally {
        if (data) {
            data.brokerSecret = null;
            data.publicationSecret = null;
            data.transferKey = null;
        }
    }
}
