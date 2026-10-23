import path from "node:path";
import os from "node:os";
import { describe, expect, it, vi } from "vitest";
import { verifySystemPlatformRuntime } from "./platform-production-system-acceptance.mjs";

const keyId = "native-ed25519-" + "a".repeat(32);
function fixture() {
    const root = os.tmpdir();
    const configuration = { schemaVersion: 1, siteOrigin: "https://platform.invalid", expectedNativeKeyId: keyId,
        powerShellExecutable: path.join(root, "pwsh.exe"), scriptsDirectory: root, settingsRoot: root, storageRoot: root };
    const pool = { query: vi.fn(async (sql) => sql.startsWith("SELECT")
        ? [[{ ready: 1, database_name: "fixture" }]] : [[{ Value: "TLS_AES_256_GCM_SHA384" }]]), end: vi.fn(async () => {}) };
    const runtime = { accountEncryptionKey: Buffer.alloc(32, 1), settings: { database: "fixture", runtime: { password: "private-db" } } };
    const workers = { transferKey: Buffer.alloc(32, 2), brokerSecret: "private-worker", publicationSecret: "private-publisher" };
    const signer = { jwks: { keys: [{ kid: keyId, crv: "Ed25519" }] }, close: vi.fn() };
    const mailer = { verify: vi.fn(async () => {}), close: vi.fn(), sendVerification: vi.fn() };
    const overrides = { verifyPrivateUploadDirectory: vi.fn(async (directory) => path.resolve(directory)),
        currentIdentity: vi.fn(async () => "S-1-5-18"), loadWindowsPlatformSecrets: vi.fn(async () => runtime),
        createPlatformPool: vi.fn(async () => pool), loadWindowsNativeSecrets: vi.fn(async () => ({})),
        createNativeIdentitySigner: vi.fn(() => signer), loadWindowsDownloadSecrets: vi.fn(async () => Buffer.alloc(32, 3)),
        loadProductionWorkerSecrets: vi.fn(async () => workers),
        loadWindowsMailSecrets: vi.fn(async () => ({ password: "private-mail" })), createVerificationMailer: vi.fn(() => mailer) };
    return { configuration, overrides, pool, runtime, workers, signer, mailer };
}

describe("SYSTEM read-only platform acceptance", () => {
    it("checks protected runtime dependencies without sending mail or mutating the database", async () => {
        const f = fixture();
        const result = await verifySystemPlatformRuntime(f.configuration, f.overrides);
        expect(result).toMatchObject({ status: "passed", checks: 14, nativePublicKeyId: keyId,
            databaseWrites: false, emailSent: false, websiteStarted: false, liveConfigurationChanged: false });
        expect(f.pool.query.mock.calls.map(([sql]) => sql)).toEqual([
            "SELECT 1 AS ready, DATABASE() AS database_name", "SHOW SESSION STATUS LIKE 'Ssl_cipher'",
        ]);
        expect(f.mailer.sendVerification).not.toHaveBeenCalled();
        expect(f.pool.end).toHaveBeenCalledOnce();
        expect(f.runtime.accountEncryptionKey.every((byte) => byte === 0)).toBe(true);
        expect(f.workers.transferKey.every((byte) => byte === 0)).toBe(true);
        expect(JSON.stringify(result)).not.toContain("private-");
    });

    it("refuses another Windows identity before loading any protected settings", async () => {
        const f = fixture();
        f.overrides.currentIdentity.mockResolvedValue("S-1-5-21-user");
        expect(await verifySystemPlatformRuntime(f.configuration, f.overrides)).toMatchObject({ status: "failed", stage: "system-identity" });
        expect(f.overrides.loadWindowsPlatformSecrets).not.toHaveBeenCalled();
    });

    it("rejects non-private storage before loading service credentials", async () => {
        const f = fixture();
        f.overrides.verifyPrivateUploadDirectory.mockRejectedValue(new Error("private ACL failure"));
        expect(await verifySystemPlatformRuntime(f.configuration, f.overrides)).toMatchObject({ status: "failed", stage: "private-storage-acls" });
        expect(f.overrides.loadWindowsPlatformSecrets).not.toHaveBeenCalled();
    });

    it("fails a missing TLS cipher and closes the pool", async () => {
        const f = fixture();
        f.pool.query.mockImplementation(async (sql) => sql.startsWith("SELECT")
            ? [[{ ready: 1, database_name: "fixture" }]] : [[{ Value: "" }]]);
        expect(await verifySystemPlatformRuntime(f.configuration, f.overrides)).toMatchObject({ status: "failed", stage: "mysql-tls-read-only" });
        expect(f.pool.end).toHaveBeenCalledOnce();
    });

    it("rejects a changed native signing identity", async () => {
        const f = fixture();
        f.signer.jwks.keys[0].kid = "unexpected";
        expect(await verifySystemPlatformRuntime(f.configuration, f.overrides)).toMatchObject({ status: "failed", stage: "native-identity-loader" });
        expect(f.signer.close).toHaveBeenCalledOnce();
    });

    it("suppresses SMTP diagnostics and still disposes all resources", async () => {
        const f = fixture();
        f.mailer.verify.mockRejectedValue(new Error("private-smtp-token"));
        const result = await verifySystemPlatformRuntime(f.configuration, f.overrides);
        expect(result).toMatchObject({ status: "failed", stage: "smtp-verify-no-send" });
        expect(JSON.stringify(result)).not.toContain("private-smtp-token");
        expect(f.mailer.close).toHaveBeenCalledOnce();
        expect(f.pool.end).toHaveBeenCalledOnce();
    });

    it("does not report success if shutdown fails", async () => {
        const f = fixture();
        f.pool.end.mockRejectedValue(new Error("secret pool diagnostic"));
        expect(await verifySystemPlatformRuntime(f.configuration, f.overrides)).toMatchObject({ status: "failed", stage: "resource-cleanup" });
    });
});
