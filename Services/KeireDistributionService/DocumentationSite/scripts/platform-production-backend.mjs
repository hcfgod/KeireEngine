import path from "node:path";
import { lstat, realpath } from "node:fs/promises";
import { PlatformBackend } from "../Source/lib/platform-backend.mjs";
import { loadWindowsPlatformSecrets } from "../Source/lib/platform-secrets.mjs";
import { loadWindowsMailSecrets, createVerificationMailer } from "../Source/lib/platform-mail.mjs";
import { windowsNativeServiceFactory } from "../Source/lib/platform-native-factory.mjs";
import { windowsGitHubServiceFactory } from "../Source/lib/platform-github-factory.mjs";
import { createLocalUploadService } from "../Source/lib/platform-upload-service.mjs";
import { createLocalValidatorService } from "../Source/lib/platform-validator-service.mjs";
import { createLocalPublicationService } from "../Source/lib/platform-publication-service.mjs";
import { verifyPrivateUploadDirectory } from "../Source/lib/platform-upload-directory.mjs";
import { LocalBlobStore } from "../Source/lib/local-blob-store.ts";
import { acquireProductionWriterLease } from "./platform-production-writer.mjs";
import { loadProductionWorkerSecrets } from "./platform-production-worker-secrets.mjs";

export function productionConfiguration(input) {
    const required = ["webRoot", "scriptsDirectory", "powerShellExecutable", "mysqlSettingsDirectory", "mailSettingsDirectory",
        "nativeSettingsDirectory", "downloadSettingsDirectory", "githubSettingsDirectory", "workerSettingsDirectory",
        "workerDecryptScriptPath", "spoolDirectory", "packageDirectory", "evidenceDirectory"];
    if (!input || input.schemaVersion !== 1 || Object.keys(input).some((key) => ![...required, "schemaVersion", "siteOrigin", "port"].includes(key)) ||
        required.some((key) => typeof input[key] !== "string" || !path.isAbsolute(input[key])) ||
        !Number.isInteger(input.port) || input.port < 1024 || input.port > 65535) {
        throw new Error("Production MySQL configuration requires explicit absolute paths and a loopback port.");
    }
    const origin = new URL(input.siteOrigin);
    if (origin.protocol !== "https:" || origin.origin !== input.siteOrigin) {
        throw new Error("Production MySQL configuration requires a canonical HTTPS origin.");
    }
    const paths = Object.fromEntries(required.map((name) => [name, path.resolve(input[name])]));
    const nested = (parent, child) => {
        const relative = path.relative(parent, child);
        return relative === "" || (!relative.startsWith(`..${path.sep}`) && relative !== ".." && !path.isAbsolute(relative));
    };
    const storage = [paths.spoolDirectory, paths.packageDirectory, paths.evidenceDirectory];
    if (storage.some((directory) => nested(paths.webRoot, directory) || nested(directory, paths.webRoot)) ||
        storage.some((directory, index) => storage.some((other, otherIndex) => index !== otherIndex && nested(directory, other)))) {
        throw new Error("Private storage must be separate from the website and from each other.");
    }
    const secretRoots = required.filter((name) => name.endsWith("SettingsDirectory")).map((name) => paths[name]);
    if (secretRoots.some((directory) => nested(paths.webRoot, directory) || storage.some((root) => nested(root, directory)))) {
        throw new Error("Protected settings must not be inside web or asset storage roots.");
    }
    return Object.freeze({ ...paths, schemaVersion: 1, siteOrigin: origin.origin, port: input.port });
}

async function verifyWebDirectory(directory) {
    const information = await lstat(directory);
    const canonical = await realpath(directory);
    const normalize = (value) => process.platform === "win32" ? value.toLowerCase() : value;
    if (!information.isDirectory() || information.isSymbolicLink() || normalize(canonical) !== normalize(directory)) {
        throw new Error("Website deployment must be an ordinary canonical directory.");
    }
}

/** Constructs the complete production service graph; does not listen, migrate schemas, provision ACLs or run validator executables. */
export async function createProductionBackend(input, overrides = {}) {
    const configuration = productionConfiguration(input);
    const dependencies = { PlatformBackend, loadWindowsPlatformSecrets, loadWindowsMailSecrets, createVerificationMailer,
        windowsNativeServiceFactory, windowsGitHubServiceFactory, createLocalUploadService, createLocalValidatorService,
        createLocalPublicationService, verifyPrivateUploadDirectory, verifyWebDirectory, LocalBlobStore, loadProductionWorkerSecrets, acquireProductionWriterLease, ...overrides };
    await dependencies.verifyWebDirectory(configuration.webRoot);
    const helper = (name) => path.join(configuration.scriptsDirectory, name);
    const permissionOptions = { powerShellExecutable: configuration.powerShellExecutable,
        verificationScriptPath: helper("test-private-upload-directory.ps1") };
    for (const directory of [configuration.spoolDirectory, configuration.packageDirectory, configuration.evidenceDirectory]) {
        await dependencies.verifyPrivateUploadDirectory(directory, permissionOptions);
    }
    const packageStore = new dependencies.LocalBlobStore(configuration.packageDirectory);
    const evidenceStore = new dependencies.LocalBlobStore(configuration.evidenceDirectory);
    const secretOptions = (settingsDirectory, file) => ({ powerShellExecutable: configuration.powerShellExecutable,
        settingsDirectory, decryptScriptPath: helper(file), siteOrigin: configuration.siteOrigin });
    const workerOptions = { ...secretOptions(configuration.workerSettingsDirectory, "unused"),
        decryptScriptPath: configuration.workerDecryptScriptPath };
    const lease = await dependencies.acquireProductionWriterLease(configuration.spoolDirectory);
    let backend;
    try {
        backend = new dependencies.PlatformBackend({
            provider: "mysql",
            createAdminEvidenceStore: () => evidenceStore,
            loadLocalSecrets: () => dependencies.loadWindowsPlatformSecrets(secretOptions(configuration.mysqlSettingsDirectory,
                "read-local-platform-runtime.ps1")),
            createMailer: async () => {
                const settings = await dependencies.loadWindowsMailSecrets(secretOptions(configuration.mailSettingsDirectory,
                    "read-local-platform-mail.ps1"));
                try {
                    return dependencies.createVerificationMailer(settings, undefined, configuration.siteOrigin);
                } finally {
                    settings.password = null;
                }
            },
            createGitHub: dependencies.windowsGitHubServiceFactory(secretOptions(configuration.githubSettingsDirectory,
                "read-local-platform-github.ps1")),
            createNative: dependencies.windowsNativeServiceFactory({ ...secretOptions(configuration.nativeSettingsDirectory,
                "read-local-platform-native.ps1"), clientId: "keire-hub-desktop", downloads: {
                settingsDirectory: configuration.downloadSettingsDirectory,
                decryptScriptPath: helper("read-local-platform-downloads.ps1"), packageStore,
            } }),
            createUploads: ({ authentication }) => dependencies.createLocalUploadService({ authentication,
                spoolDirectory: configuration.spoolDirectory, blobDirectory: configuration.packageDirectory,
                siteOrigin: configuration.siteOrigin, permissionOptions }),
            createValidator: async ({ pool }) => {
                const secrets = await dependencies.loadProductionWorkerSecrets(workerOptions);
                try {
                    return dependencies.createLocalValidatorService({ pool, packageStore, evidenceStore,
                        siteOrigin: configuration.siteOrigin, brokerSecret: secrets.brokerSecret, transferKey: secrets.transferKey });
                } finally {
                    secrets.transferKey.fill(0);
                    secrets.brokerSecret = null;
                    secrets.publicationSecret = null;
                }
            },
            createPublication: async ({ pool }) => {
                const secrets = await dependencies.loadProductionWorkerSecrets(workerOptions);
                try {
                    return dependencies.createLocalPublicationService({ pool, packageStore, evidenceStore,
                        siteOrigin: configuration.siteOrigin, queueSecret: secrets.publicationSecret });
                } finally {
                    secrets.transferKey.fill(0);
                    secrets.brokerSecret = null;
                    secrets.publicationSecret = null;
                }
            },
        });
    } catch (error) {
        await lease.release();
        throw error;
    }
    const closeBackend = backend.close.bind(backend);
    let shutdown;
    backend.close = () => {
        shutdown ??= closeBackend().finally(() => lease.release());
        return shutdown;
    };
    try {
        await backend.withBackend(() => undefined);
        return backend;
    } catch {
        await backend.close().catch(() => {});
        throw new Error("Production MySQL services failed to initialize; protected diagnostics were suppressed.");
    }
}
