import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { randomUUID } from "node:crypto";
import { verifyPrivateUploadDirectory } from "../Source/lib/platform-upload-directory.mjs";
import { loadWindowsPlatformSecrets } from "../Source/lib/platform-secrets.mjs";
import { createPlatformPool } from "../Source/lib/platform-mysql.mjs";
import { loadWindowsNativeSecrets } from "../Source/lib/platform-native-secrets.mjs";
import { createNativeIdentitySigner } from "../Source/lib/platform-native-identity.mjs";
import { loadWindowsDownloadSecrets } from "../Source/lib/platform-download-secrets.mjs";
import { createDownloadCapabilities } from "../Source/lib/platform-download-capabilities.mjs";
import { loadProductionWorkerSecrets } from "./platform-production-worker-secrets.mjs";
import { loadWindowsMailSecrets, createVerificationMailer } from "../Source/lib/platform-mail.mjs";
import { loadWindowsGitHubSecrets } from "../Source/lib/platform-github-secrets.mjs";

const execute = promisify(execFile);
const requiredPaths = ["powerShellExecutable", "scriptsDirectory", "settingsRoot", "storageRoot"];

async function currentIdentity(powerShellExecutable) {
    const result = await execute(powerShellExecutable, ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command",
        "[Console]::Out.Write([Security.Principal.WindowsIdentity]::GetCurrent().User.Value)"],
    { shell: false, windowsHide: true, timeout: 10000, maxBuffer: 4096, encoding: "utf8" });
    return result.stdout.trim();
}

/** Read-only service-identity acceptance. Does not start a website, run workers, send mail or modify application data. */
export async function verifySystemPlatformRuntime(configuration, overrides = {}) {
    const dependencies = { currentIdentity, loadWindowsPlatformSecrets, createPlatformPool, loadWindowsNativeSecrets,
        createNativeIdentitySigner, loadWindowsDownloadSecrets, createDownloadCapabilities, loadProductionWorkerSecrets,
        loadWindowsMailSecrets, createVerificationMailer, verifyPrivateUploadDirectory, loadWindowsGitHubSecrets, ...overrides };
    let stage = "configuration";
    let checks = 0;
    let pool;
    let runtime;
    let signer;
    let downloadKey;
    let capabilities;
    let workerSecrets;
    let mailSettings;
    let mailer;
    let githubSettings;
    let passed = false;
    let publicKeyId;
    let failureStage;
    let failureCode;
    let mysqlOperation;
    try {
        const allowed = ["schemaVersion", "siteOrigin", "expectedNativeKeyId", "githubSettingsDirectory", "expectedGitHubClientId", ...requiredPaths];
        if (!configuration || configuration.schemaVersion !== 1 || Object.keys(configuration).some((name) => !allowed.includes(name)) ||
            requiredPaths.some((name) => typeof configuration[name] !== "string" || !path.isAbsolute(configuration[name])) ||
            !/^native-ed25519-[a-f0-9]{32}$/.test(configuration.expectedNativeKeyId ?? "")) {
            throw new Error();
        }
        const origin = new URL(configuration.siteOrigin);
        const checkGitHub = Object.hasOwn(configuration, "githubSettingsDirectory") || Object.hasOwn(configuration, "expectedGitHubClientId");
        if (checkGitHub && (typeof configuration.githubSettingsDirectory !== "string" ||
            !path.isAbsolute(configuration.githubSettingsDirectory) ||
            !/^[A-Za-z0-9]{10,80}$/.test(configuration.expectedGitHubClientId ?? ""))) {
            throw new Error();
        }
        if (origin.protocol !== "https:" || origin.origin !== configuration.siteOrigin) {
            throw new Error();
        }
        const assert = (condition) => {
            if (!condition) {
                throw new Error();
            }
            checks++;
        };
        const options = (directory, reader) => ({ powerShellExecutable: configuration.powerShellExecutable,
            decryptScriptPath: path.join(configuration.scriptsDirectory, reader),
            settingsDirectory: path.join(configuration.settingsRoot, directory), siteOrigin: configuration.siteOrigin });
        stage = "system-identity";
        assert(await dependencies.currentIdentity(configuration.powerShellExecutable) === "S-1-5-18");
        stage = "private-storage-acls";
        for (const name of ["Spool", "Packages", "Evidence"]) {
            const directory = path.join(configuration.storageRoot, name);
            assert(await dependencies.verifyPrivateUploadDirectory(directory, {
                powerShellExecutable: configuration.powerShellExecutable,
                verificationScriptPath: path.join(configuration.scriptsDirectory, "test-private-upload-directory.ps1"),
            }) === path.resolve(directory));
        }
        stage = "mysql-protected-loader";
        runtime = await dependencies.loadWindowsPlatformSecrets(options("PlatformMySql", "read-local-platform-runtime.ps1"));
        assert(Buffer.isBuffer(runtime.accountEncryptionKey) && runtime.accountEncryptionKey.length === 32);
        assert(!Object.hasOwn(runtime.settings, "migration"));
        stage = "mysql-tls-read-only";
        mysqlOperation = "load-ca-and-create-pool";
        pool = await dependencies.createPlatformPool(runtime.settings, "runtime");
        mysqlOperation = "connect-and-query";
        const [probe] = await pool.query("SELECT 1 AS ready, DATABASE() AS database_name");
        assert(Number(probe[0]?.ready) === 1 && probe[0]?.database_name === runtime.settings.database);
        mysqlOperation = "verify-session-cipher";
        const [tls] = await pool.query("SHOW SESSION STATUS LIKE 'Ssl_cipher'");
        assert(tls.length === 1 && typeof tls[0].Value === "string" && tls[0].Value.length > 0);
        stage = "native-identity-loader";
        const identity = await dependencies.loadWindowsNativeSecrets({ ...options("PlatformNativeIdentity", "read-local-platform-native.ps1"),
            clientId: "keire-hub-desktop" });
        signer = dependencies.createNativeIdentitySigner(identity);
        const key = signer.jwks.keys[0];
        assert(signer.jwks.keys.length === 1 && key.kid === configuration.expectedNativeKeyId && key.crv === "Ed25519" && !Object.hasOwn(key, "d"));
        publicKeyId = key.kid;
        stage = "download-key-local-roundtrip";
        downloadKey = await dependencies.loadWindowsDownloadSecrets(options("PlatformDownloads", "read-local-platform-downloads.ps1"));
        capabilities = dependencies.createDownloadCapabilities({ siteOrigin: configuration.siteOrigin, key: downloadKey });
        downloadKey.fill(0);
        const grant = randomUUID();
        const link = capabilities.issue(grant, new Date(Date.now() + 60000).toISOString());
        assert(capabilities.verify(link)?.grantId === grant);
        const tampered = new URL(link);
        tampered.searchParams.set("signature", "a".repeat(43));
        assert(capabilities.verify(tampered.toString()) === null);
        capabilities.close();
        assert(capabilities.verify(link) === null);
        stage = "worker-protected-loader";
        workerSecrets = await dependencies.loadProductionWorkerSecrets(options("PlatformWorkers", "read-local-platform-workers.ps1"));
        assert(Buffer.isBuffer(workerSecrets.transferKey) && workerSecrets.transferKey.length === 32 &&
            typeof workerSecrets.brokerSecret === "string" && workerSecrets.brokerSecret !== workerSecrets.publicationSecret);
        stage = "smtp-protected-loader";
        mailSettings = await dependencies.loadWindowsMailSecrets(options("PlatformMail", "read-local-platform-mail.ps1"));
        mailer = dependencies.createVerificationMailer(mailSettings, undefined, configuration.siteOrigin);
        mailSettings.password = null;
        stage = "smtp-verify-no-send";
        await mailer.verify();
        checks++;
        if (checkGitHub) {
            stage = "github-protected-loader";
            githubSettings = await dependencies.loadWindowsGitHubSecrets({
                ...options("PlatformGitHub", "read-local-platform-github.ps1"),
                settingsDirectory: configuration.githubSettingsDirectory,
            });
            assert(githubSettings.clientId === configuration.expectedGitHubClientId);
            assert(githubSettings.siteOrigin === configuration.siteOrigin && Buffer.isBuffer(githubSettings.clientSecret) &&
                githubSettings.clientSecret.length >= 20);
        }
        passed = true;
    } catch (error) {
        failureStage = stage;
        // Only known constant codes are safe evidence; driver messages can contain credentials or SQL.
        const safeCodes = new Set(["ENOENT", "EACCES", "EPERM", "ECONNREFUSED", "ETIMEDOUT",
            "ER_ACCESS_DENIED_ERROR", "ER_DBACCESS_DENIED_ERROR", "ER_BAD_DB_ERROR",
            "ER_PARSE_ERROR", "HANDSHAKE_SSL_ERROR", "ERR_TLS_CERT_ALTNAME_INVALID",
            "CERT_HAS_EXPIRED", "UNABLE_TO_VERIFY_LEAF_SIGNATURE", "SELF_SIGNED_CERT_IN_CHAIN"]);
        if (stage === "mysql-tls-read-only") {
            failureCode = safeCodes.has(error?.code) ? error.code : "unclassified";
        }
    } finally {
        for (const cleanup of [() => mailer?.close(), () => capabilities?.close(), () => signer?.close(), () => pool?.end()]) {
            try {
                await cleanup();
            } catch {
                passed = false;
                failureStage ??= "resource-cleanup";
            }
        }
        runtime?.accountEncryptionKey?.fill(0);
        githubSettings?.clientSecret?.fill(0);
        if (runtime?.settings?.runtime) {
            runtime.settings.runtime.password = null;
        }
        downloadKey?.fill(0);
        workerSecrets?.transferKey?.fill(0);
        if (workerSecrets) {
            workerSecrets.brokerSecret = null;
            workerSecrets.publicationSecret = null;
        }
        if (mailSettings) {
            mailSettings.password = null;
        }
    }
    return { status: passed ? "passed" : "failed", checks, ...(passed ? { nativePublicKeyId: publicKeyId } : { stage: failureStage }),
        ...(failureCode ? { failureCode, mysqlOperation } : {}),
        databaseWrites: false, emailSent: false, websiteStarted: false, liveConfigurationChanged: false };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
    let result;
    try {
        if (process.argv.length !== 4 || !path.isAbsolute(process.argv[2]) || !path.isAbsolute(process.argv[3])) {
            throw new Error();
        }
        const contents = await readFile(process.argv[2]);
        if (contents.length > 16384) {
            throw new Error();
        }
        result = await verifySystemPlatformRuntime(JSON.parse(contents.toString("utf8").replace(/^\uFEFF/, "")));
        await writeFile(process.argv[3], JSON.stringify(result, null, 2), { flag: "wx", mode: 0o600 });
        console.log(JSON.stringify(result));
        if (result.status !== "passed") {
            process.exitCode = 1;
        }
    } catch {
        console.error(JSON.stringify({ status: "failed", stage: "acceptance-configuration-or-evidence-write",
            liveConfigurationChanged: false, emailSent: false }));
        process.exitCode = 1;
    }
}
