import { execFile } from "node:child_process";
import { mkdir, mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { createLocalUploadService } from "../Source/lib/platform-upload-service.mjs";

export async function createQaUploadService(authentication, siteOrigin, powerShellExecutable) {
    const directory = await mkdtemp(path.join(tmpdir(), "keire-http-upload-"));
    const scripts = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../scripts");
    async function cleanup() {
        if (path.dirname(path.resolve(directory)) !== path.resolve(tmpdir())
            || !path.basename(directory).startsWith("keire-http-upload-")) {
            throw new Error("Refusing cleanup outside the isolated upload fixture.");
        }
        await rm(directory, { recursive: true, force: true });
    }
    try {
        if (process.platform === "win32") {
            await promisify(execFile)(powerShellExecutable, [
                "-NoProfile", "-NonInteractive", "-File", path.join(scripts, "protect-qa-upload-directory.ps1"),
                "-Directory", directory,
            ], { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 4096 });
        }
        const spoolDirectory = path.join(directory, "spool");
        const blobDirectory = path.join(directory, "blobs");
        await mkdir(spoolDirectory, { mode: 0o700 });
        await mkdir(blobDirectory, { mode: 0o700 });
        const service = await createLocalUploadService({
            authentication, spoolDirectory, blobDirectory, siteOrigin,
            permissionOptions: { powerShellExecutable,
                verificationScriptPath: path.join(scripts, "test-private-upload-directory.ps1") },
        });
        let shutdown;
        return { ...service, close() {
            shutdown ??= service.close().then(cleanup);
            return shutdown;
        } };
    } catch (error) {
        await cleanup();
        throw error;
    }
}
