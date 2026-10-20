import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";
import path from "node:path";

export async function verifyCSharpValidatorHttp(http, brokerSecret, registerPublicKey) {
    const executable = process.env.KEIRE_QA_DOTNET;
    assert.ok(path.isAbsolute(executable ?? ""), "Set the isolated acceptance .NET executable.");
    const assembly = fileURLToPath(new URL(
        "../../tests/KeireMarketplaceValidator.Tests/bin/Release/net10.0/KeireMarketplaceValidator.Tests.dll",
        import.meta.url));
    const child = spawn(executable, [assembly], {
        env: { ...process.env, KEIRE_TEST_LIVE_VALIDATOR: "1" },
        windowsHide: true, stdio: ["pipe", "pipe", "pipe"],
    });
    const completion = new Promise((resolve, reject) => {
        child.once("error", reject);
        child.once("exit", (code) => resolve(code));
    });
    // Suppress diagnostics: neither credentials nor signed transfer URLs belong in logs.
    child.stderr.resume();
    const timeout = setTimeout(() => child.kill(), 50000);
    const lines = createInterface({ input: child.stdout });
    let stage = "csharp-start";
    try {
        child.stdin.write(`${JSON.stringify({ origin: http.origin, brokerSecret })}\n`);
        let registered = false;
        let passed = false;
        for await (const line of lines) {
            stage = "csharp-public-key";
            const document = JSON.parse(line);
            if (!registered) {
                await registerPublicKey(document);
                registered = true;
                child.stdin.end("ready\n");
                stage = "csharp-workflow";
            } else {
                assert.equal(document.status, "csharp-validator-https-passed");
                passed = true;
            }
        }
        assert.equal(await completion, 0, "C# validator acceptance process failed.");
        assert.ok(registered && passed, "C# validator did not complete the HTTPS workflow.");
    } catch (error) {
        error.fixtureStage = stage;
        throw error;
    } finally {
        clearTimeout(timeout);
        lines.close();
        if (child.exitCode === null) {
            child.kill();
        }
        await completion;
    }
}
