import { mkdtemp, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const siteRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repositoryRoot = path.resolve(siteRoot, "..", "..", "..");
const project = path.join(repositoryRoot, "Docs", "Manual", "Examples", "Keire.ManualExamples.csproj");
const artifacts = await mkdtemp(path.join(os.tmpdir(), "keire-manual-examples-"));

function run(command, arguments_) {
    return new Promise((resolve, reject) => {
        const child = spawn(command, arguments_, {
            cwd: repositoryRoot,
            stdio: "inherit",
            windowsHide: true,
        });
        child.on("error", reject);
        child.on("exit", (code, signal) => {
            if (code === 0) {
                resolve();
                return;
            }
            reject(new Error(`Manual example compilation failed (${signal ?? `exit ${code}`}).`));
        });
    });
}

try {
    await run("dotnet", ["build", project, "--artifacts-path", artifacts, "--nologo", "--verbosity", "minimal"]);
    console.log("Kéire user-manual C# examples compile against the current managed API.");
} finally {
    await rm(artifacts, { recursive: true, force: true });
}
