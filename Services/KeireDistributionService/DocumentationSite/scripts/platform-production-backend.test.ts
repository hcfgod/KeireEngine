import path from "node:path";
import os from "node:os";
import { describe, expect, it, vi } from "vitest";
import { productionConfiguration, createProductionBackend } from "./platform-production-backend.mjs";

function configuration() {
    const root = path.join(os.tmpdir(), "production-composition-fixture-not-created");
    const value = { schemaVersion: 1, siteOrigin: "https://platform.invalid", port: 19490 };
    for (const name of ["webRoot", "scriptsDirectory", "powerShellExecutable", "mysqlSettingsDirectory", "mailSettingsDirectory",
        "nativeSettingsDirectory", "downloadSettingsDirectory", "githubSettingsDirectory", "workerSettingsDirectory",
        "workerDecryptScriptPath", "spoolDirectory", "packageDirectory", "evidenceDirectory"]) {
        value[name] = path.join(root, name);
    }
    return value;
}
function dependencies() {
    let options;
    const close = vi.fn(async () => {});
    const initialize = vi.fn(async () => {});
    const workerSecrets = [];
    const overrides = {
        PlatformBackend: class {
            constructor(input) { options = input; }
            withBackend = initialize;
            close = close;
        },
        acquireProductionWriterLease: vi.fn(async () => ({ release: vi.fn(async () => {}) })),
        verifyWebDirectory: vi.fn(async () => {}),
        verifyPrivateUploadDirectory: vi.fn(async (directory) => directory),
        LocalBlobStore: class { constructor(directory) { this.directory = directory; } },
        loadWindowsPlatformSecrets: vi.fn(), loadWindowsMailSecrets: vi.fn(async () => ({ password: "synthetic" })),
        createVerificationMailer: vi.fn(() => ({ mail: true })),
        windowsNativeServiceFactory: vi.fn(() => "native-factory"), windowsGitHubServiceFactory: vi.fn(() => "github-factory"),
        createLocalUploadService: vi.fn(), createLocalValidatorService: vi.fn(() => ({ validator: true })),
        createLocalPublicationService: vi.fn(() => ({ publication: true })),
        loadProductionWorkerSecrets: vi.fn(async () => {
            const secrets = { transferKey: Buffer.alloc(32, 3), brokerSecret: "validator", publicationSecret: "publication" };
            workerSecrets.push(secrets);
            return secrets;
        }),
    };
    return { overrides, close, initialize, workerSecrets, options: () => options };
}

describe("production MySQL composition", () => {
    it("requires complete explicit configuration and disjoint private storage", () => {
        expect(productionConfiguration(configuration()).siteOrigin).toBe("https://platform.invalid");
        expect(() => productionConfiguration({ ...configuration(), port: 0 })).toThrow();
        expect(() => productionConfiguration({ ...configuration(), siteOrigin: "https://platform.invalid/" })).toThrow();
        expect(() => productionConfiguration({ ...configuration(), githubSettingsDirectory: "relative" })).toThrow();
        const nested = configuration();
        nested.packageDirectory = path.join(nested.webRoot, "private");
        expect(() => productionConfiguration(nested)).toThrow("storage");
        const secrets = configuration();
        secrets.mysqlSettingsDirectory = path.join(secrets.packageDirectory, "secret");
        expect(() => productionConfiguration(secrets)).toThrow("settings");
        expect(() => productionConfiguration({ ...configuration(), captureMail: true })).toThrow();
    });

    it("composes all production services with owned protected secrets and shared immutable stores", async () => {
        const f = dependencies();
        const config = configuration();
        const backend = await createProductionBackend(config, f.overrides);
        const options = f.options();
        expect(options.provider).toBe("mysql");
        expect(options.createGitHub).toBe("github-factory");
        expect(options.createNative).toBe("native-factory");
        expect(f.overrides.verifyPrivateUploadDirectory).toHaveBeenCalledTimes(3);
        expect(await options.createMailer()).toEqual({ mail: true });
        const pool = {};
        expect(await options.createValidator({ pool })).toEqual({ validator: true });
        expect(await options.createPublication({ pool })).toEqual({ publication: true });
        const validator = f.overrides.createLocalValidatorService.mock.calls[0][0];
        const publication = f.overrides.createLocalPublicationService.mock.calls[0][0];
        expect(validator.packageStore).toBe(publication.packageStore);
        expect(validator.evidenceStore).toBe(publication.evidenceStore);
        expect(options.createAdminEvidenceStore()).toBe(validator.evidenceStore);
        expect(f.workerSecrets.every((secret) => secret.transferKey.every((byte) => byte === 0) && secret.brokerSecret === null)).toBe(true);
        await backend.close();
        expect(f.close).toHaveBeenCalledTimes(1);
    });

    it("fails before creating services if private directory ACL verification fails", async () => {
        const f = dependencies();
        f.overrides.verifyPrivateUploadDirectory.mockRejectedValue(new Error("invalid ACL"));
        await expect(createProductionBackend(configuration(), f.overrides)).rejects.toThrow("invalid ACL");
        expect(f.options()).toBeUndefined();
    });

    it("cleans up failed initialization and suppresses credential-bearing diagnostics", async () => {
        const f = dependencies();
        f.initialize.mockRejectedValue(new Error("password-in-database-error"));
        await expect(createProductionBackend(configuration(), f.overrides)).rejects.toThrow("protected diagnostics were suppressed");
        expect(f.close).toHaveBeenCalledOnce();
    });

    it("clears loaded worker credentials even when a service fails to construct", async () => {
        const f = dependencies();
        await createProductionBackend(configuration(), f.overrides);
        f.overrides.createLocalValidatorService.mockImplementation(() => { throw new Error("fixture"); });
        await expect(f.options().createValidator({ pool: {} })).rejects.toThrow("fixture");
        expect(f.workerSecrets[0].transferKey.every((byte) => byte === 0)).toBe(true);
    });
});
