import { execFile } from "node:child_process";
import { lstat, realpath } from "node:fs/promises";
import path from "node:path";
import { promisify } from "node:util";

const execute = promisify(execFile);

/** Read-only startup check. Directory creation and ACL provisioning belong to installation. */
export async function verifyPrivateUploadDirectory(directory, { powerShellExecutable, verificationScriptPath } = {}) {
    if (typeof directory !== "string" || !path.isAbsolute(directory)) {
        throw new Error("Upload storage requires an absolute private directory.");
    }
    const resolved = path.resolve(directory);
    const information = await lstat(resolved);
    if (!information.isDirectory() || information.isSymbolicLink()) {
        throw new Error("Upload storage must be an ordinary directory.");
    }
    const canonical = await realpath(resolved);
    const normalize = (value) => process.platform === "win32" ? value.toLowerCase() : value;
    if (normalize(canonical) !== normalize(resolved)) {
        throw new Error("Upload storage must not traverse symbolic links.");
    }
    if (process.platform !== "win32") {
        if ((information.mode & 0o077) !== 0 || information.uid !== process.getuid()) {
            throw new Error("Upload storage must be owned by the service account and restricted to it.");
        }
        return resolved;
    }
    if (![powerShellExecutable, verificationScriptPath].every((value) => typeof value === "string" && path.isAbsolute(value))) {
        throw new Error("Windows upload storage requires an ACL verification helper.");
    }
    try {
        const result = await execute(powerShellExecutable, [
            "-NoLogo", "-NoProfile", "-NonInteractive", "-File", verificationScriptPath, "-Directory", resolved,
        ], { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 4096, encoding: "utf8" });
        if (JSON.parse(result.stdout.replace(/^\uFEFF/, "")).privateDirectory !== true) {
            throw new Error("Invalid directory verification result.");
        }
    } catch {
        throw new Error("Private upload directory validation failed. Check the service account and directory permissions.");
    }
    return resolved;
}
