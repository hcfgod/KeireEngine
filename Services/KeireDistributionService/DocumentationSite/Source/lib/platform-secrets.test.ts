import path from "node:path";
import { describe, expect, it, vi } from "vitest";

const { execute } = vi.hoisted(() => ({ execute: vi.fn() }));
vi.mock("node:child_process", async () => {
    const { promisify } = await import("node:util");
    const execFile = Object.assign(() => undefined, { [promisify.custom]: execute });
    return { execFile };
});
import { loadWindowsPlatformSecrets } from "./platform-secrets.mjs";

const options = {
    powerShellExecutable: path.resolve("tools/pwsh.exe"),
    decryptScriptPath: path.resolve("scripts/read.ps1"),
    settingsDirectory: path.resolve("protected"),
};

describe("protected runtime secret loading", () => {
    it("uses an argument array and a bounded hidden child without exposing migration credentials", async () => {
        execute.mockResolvedValueOnce({ stdout: JSON.stringify({ settings: { runtime: { user: "runtime" } },
            accountEncryptionKey: Buffer.alloc(32, 7).toString("base64") }) });
        const result = await loadWindowsPlatformSecrets(options);
        expect(result.accountEncryptionKey.equals(Buffer.alloc(32, 7))).toBe(true);
        expect(result.settings.migration).toBeUndefined();
        expect(execute).toHaveBeenLastCalledWith(options.powerShellExecutable,
            ["-NoLogo", "-NoProfile", "-NonInteractive", "-File", options.decryptScriptPath,
                "-SettingsDirectory", options.settingsDirectory],
            expect.objectContaining({ shell: false, windowsHide: true, maxBuffer: 32768, timeout: 15000 }));
        result.accountEncryptionKey.fill(0);
    });

    it.each([
        "not JSON",
        JSON.stringify({ settings: {}, accountEncryptionKey: "short" }),
        JSON.stringify({ settings: { migration: {} }, accountEncryptionKey: Buffer.alloc(32).toString("base64") }),
    ])("rejects malformed or overprivileged payloads without echoing them", async (stdout) => {
        execute.mockResolvedValueOnce({ stdout });
        await expect(loadWindowsPlatformSecrets(options)).rejects.toThrow("Protected platform runtime settings are unavailable.");
    });

    it("suppresses child error output, including secrets in the error message", async () => {
        execute.mockRejectedValueOnce(new Error("secret stdout and credentials"));
        try {
            await loadWindowsPlatformSecrets(options);
            expect.fail("expected failure");
        } catch (error) {
            expect(String(error)).not.toContain("credentials");
            expect(error.cause).toBeUndefined();
        }
    });

    it("rejects relative executable paths before starting a process", async () => {
        const calls = execute.mock.calls.length;
        await expect(loadWindowsPlatformSecrets({ ...options, powerShellExecutable: "pwsh" })).rejects.toThrow("absolute");
        expect(execute.mock.calls.length).toBe(calls);
    });
});
