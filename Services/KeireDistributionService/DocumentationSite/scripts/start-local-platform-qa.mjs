import http from "node:http";
import https from "node:https";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import assert from "node:assert/strict";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import send from "send";
import { PlatformBackend } from "../Source/lib/platform-backend.mjs";
import { loadWindowsPlatformSecrets } from "../Source/lib/platform-secrets.mjs";
import { createVerificationMailer, loadWindowsMailSecrets } from "../Source/lib/platform-mail.mjs";
import { createQaServerDrain } from "./qa-server-drain.mjs";
import { LocalPasswordAuthentication } from "../Source/lib/platform-local-auth.mjs";
import { openAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { createQaUploadService } from "./qa-upload-service.mjs";
import { windowsNativeServiceFactory } from "../Source/lib/platform-native-factory.mjs";

// Isolated loopback acceptance host. Production reverse-proxy trust and deployment remain separate gates.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const port = Number(process.env.KEIRE_QA_PORT);
const clientNamespace = process.env.KEIRE_QA_CLIENT_NAMESPACE ?? "manual";
if (!/^[a-zA-Z0-9-]{1,64}$/.test(clientNamespace)) {
    throw new Error("Invalid isolated QA client namespace.");
}
if (!Number.isInteger(port) || port < 1024 || port > 65535) {
    throw new Error("Set an explicit KEIRE_QA_PORT between 1024 and 65535.");
}
process.env.ASTRO_NODE_AUTOSTART = "disabled";
process.env.KEIRE_PLATFORM_BACKEND = "mysql";
const tlsThumbprint = process.env.KEIRE_QA_TLS_THUMBPRINT;
process.env.PUBLIC_SITE_URL = tlsThumbprint ? `https://localhost:${port}` : "https://qa.invalid.example";
let tlsOptions;
if (tlsThumbprint) {
    try {
        if (!/^[A-Fa-f0-9]{40}$/.test(tlsThumbprint) || !path.isAbsolute(process.env.KEIRE_QA_POWERSHELL ?? "")) {
            throw new Error("Invalid development TLS settings.");
        }
        const result = await promisify(execFile)(process.env.KEIRE_QA_POWERSHELL, [
            "-NoProfile", "-NonInteractive", "-File", path.resolve(root, "../scripts/read-local-platform-qa-tls.ps1"),
            "-Thumbprint", tlsThumbprint,
        ], { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 65536, encoding: "utf8" });
        const payload = JSON.parse(result.stdout.replace(/^\uFEFF/, ""));
        tlsOptions = { pfx: Buffer.from(payload.pfx, "base64"), passphrase: payload.passphrase, minVersion: "TLSv1.2" };
        payload.pfx = null;
        payload.passphrase = null;
    } catch {
        throw new Error("Isolated HTTPS setup failed; protected certificate details were suppressed.");
    }
}
const entry = await import(pathToFileURL(path.join(root, "dist/server/entry.mjs")).href);
if (entry.options.mode !== "middleware") {
    throw new Error("Build with KEIRE_NODE_ADAPTER_MODE=middleware before starting isolated QA.");
}
const mailMode = process.env.KEIRE_QA_MAIL_MODE ?? "disabled";
if (!["disabled", "capture", "gmail"].includes(mailMode) || (mailMode === "capture" && !process.send)) {
    throw new Error("QA email mode must be disabled, gmail, or capture with a private parent IPC channel.");
}
const createMailer = mailMode === "disabled" ? null : async () => {
    if (mailMode === "capture") {
        return {
            recoveryEnabled: true,
            registrationEnabled: true,
            sendRegistration: async (message) => {
                await new Promise((resolve, reject) => {
                    process.send({ type: "qa-registration-email", message }, (error) => error ? reject(error) : resolve());
                });
            },
            sendRecovery: async (message) => {
                await new Promise((resolve, reject) => {
                    process.send({ type: "qa-recovery-email", message }, (error) => error ? reject(error) : resolve());
                });
            },
            sendVerification: async (message) => {
                await new Promise((resolve, reject) => {
                    process.send({ type: "qa-verification-email", message }, (error) => error ? reject(error) : resolve());
                });
            },
            close() {},
        };
    }
    const settings = await loadWindowsMailSecrets({
        powerShellExecutable: process.env.KEIRE_QA_POWERSHELL,
        decryptScriptPath: path.resolve(root, "../scripts/read-local-platform-mail.ps1"),
        settingsDirectory: path.join(process.env.LOCALAPPDATA, "Keire", "PlatformMail"),
    });
    try {
        return createVerificationMailer(settings, undefined, process.env.PUBLIC_SITE_URL);
    } finally {
        settings.password = null;
    }
};
async function createQaAuthentication(pool, key) {
    const authentication = await LocalPasswordAuthentication.create(pool, key);
    const trackedEmail = process.env.KEIRE_QA_REGISTRATION_EMAIL;
    if (mailMode === "capture" && trackedEmail && /^registration-qa-[a-f0-9-]{36}@invalid\.example$/.test(trackedEmail)) {
        // Record only the named disposable fixture before insertion, including uncertain database outcomes.
        authentication.pool = new Proxy(pool, {
            get(target, property) {
                if (property === "execute") {
                    return async (sql, values) => {
                        if (sql.startsWith("INSERT INTO platform_registration_requests")) {
                            const pending = openAccountRecord(values[2], authentication.encryptionKey, `registration:${values[0]}`);
                            if (pending.email === trackedEmail) {
                                await new Promise((resolve, reject) => {
                                    process.send({ type: "qa-registration-request", id: values[0] },
                                        (error) => error ? reject(error) : resolve());
                                });
                            }
                        }
                        return target.execute(sql, values);
                    };
                }
                const value = Reflect.get(target, property);
                return typeof value === "function" ? value.bind(target) : value;
            },
        });
    }
    return authentication;
}

const backend = new PlatformBackend({ provider: "mysql", createMailer, createAuthentication: createQaAuthentication,
    createNative: process.env.KEIRE_QA_NATIVE_SETTINGS_DIRECTORY ? windowsNativeServiceFactory({
        powerShellExecutable: process.env.KEIRE_QA_POWERSHELL,
        decryptScriptPath: path.resolve(root, "../scripts/read-local-platform-native.ps1"),
        settingsDirectory: process.env.KEIRE_QA_NATIVE_SETTINGS_DIRECTORY,
        siteOrigin: process.env.PUBLIC_SITE_URL,
        clientId: "keire-hub-desktop",
    }) : null,
    createUploads: process.send && process.env.KEIRE_QA_UPLOADS === "isolated"
        ? ({ authentication }) => createQaUploadService(authentication, process.env.PUBLIC_SITE_URL, process.env.KEIRE_QA_POWERSHELL)
        : null,
    loadLocalSecrets: () => loadWindowsPlatformSecrets({
    powerShellExecutable: process.env.KEIRE_QA_POWERSHELL,
    decryptScriptPath: path.resolve(root, "../scripts/read-local-platform-runtime.ps1"),
    settingsDirectory: process.env.KEIRE_QA_SETTINGS_DIRECTORY,
}) });
await backend.withBackend(() => undefined);

const listener = (request, response) => {
    const serveApplication = async () => {
        try {
            await backend.withBackend(async () => {
                const completed = new Promise((resolve) => {
                    response.once("finish", resolve);
                    response.once("close", resolve);
                });
                await entry.handler(request, response, () => {
                    response.writeHead(404);
                    response.end("Not found.");
                }, {
                    platformBackend: backend,
                    // The QA listener accepts no forwarded client identity.
                    platformClientIdentity: `qa:${clientNamespace}:${request.socket.remoteAddress ?? "loopback-unknown"}`,
                });
                await completed;
            });
        } catch {
            if (!response.headersSent) {
                response.writeHead(503, { "cache-control": "no-store" });
            }
            response.end("The local platform is unavailable.");
        }
    };
    if (!["GET", "HEAD"].includes(request.method)) {
        void serveApplication();
        return;
    }
    let pathname;
    try {
        pathname = new URL(request.url, "http://127.0.0.1").pathname;
    } catch {
        response.writeHead(400);
        response.end("Invalid request.");
        return;
    }
    send(request, pathname, { root: path.join(root, "dist/client"), dotfiles: "deny", index: ["index.html"] })
        .on("error", (error) => {
            if (error.status === 404) {
                void serveApplication();
            } else {
                response.writeHead(error.status === 403 ? 403 : 400);
                response.end("Static resource unavailable.");
            }
        }).pipe(response);
};
let server;
try {
    server = tlsOptions ? https.createServer(tlsOptions, listener) : http.createServer(listener);
} finally {
    tlsOptions?.pfx.fill(0);
    tlsOptions = null;
}
server.requestTimeout = 30000;
server.headersTimeout = 10000;
const drainServer = createQaServerDrain(server, Boolean(tlsThumbprint));
let shutdown;
function close() {
    shutdown ??= (async () => {
        process.send?.({ type: "qa-shutdown-phase", phase: "connections" });
        await drainServer();
        process.send?.({ type: "qa-shutdown-phase", phase: "services" });
        await backend.close();
        process.send?.({ type: "qa-shutdown-phase", phase: "complete" });
    })();
    return shutdown;
}
process.once("SIGINT", () => { void close(); });
process.once("SIGTERM", () => { void close(); });
if (process.send) {
    process.on("message", async (message) => {
        if (message?.type === "close-qa-host") {
            await close();
            process.disconnect();
        }
    });
}
server.once("error", async () => {
    await backend.close();
    console.error("Isolated QA listener failed; diagnostics suppressed.");
    process.exitCode = 1;
});
server.listen(port, "127.0.0.1", async () => {
    console.log(JSON.stringify({ status: "local-platform-qa-listening", host: "127.0.0.1", port }));
    process.send?.({ type: "qa-host-ready" });
    if (process.argv.includes("--smoke")) {
        let stage = "sign-in";
        try {
            const base = tlsThumbprint ? `https://localhost:${port}` : `http://127.0.0.1:${port}`;
            const signIn = await fetch(`${base}/account/sign-in/`);
            assert.equal(signIn.status, 200);
            assert.match(await signIn.text(), /Sign in/);
            stage = "session";
            const session = await fetch(`${base}/account/session/`);
            assert.equal(session.status, 200);
            assert.deepEqual((await session.json()).data, { signedIn: false, assuranceLevel: null });
            stage = "challenge";
            const challenge = await fetch(`${base}/account/mfa/challenge/status/`);
            assert.equal(challenge.status, 401);
            await challenge.arrayBuffer();
            stage = "csrf";
            const csrf = await fetch(`${base}/account/session/`, { method: "POST",
                headers: { origin: "https://other.invalid", "content-type": "application/json" }, body: "{}" });
            assert.equal(csrf.status, 403);
            await csrf.arrayBuffer();
            stage = "unported";
            const unported = await fetch(`${base}/marketplace/v1/library/`);
            assert.equal(unported.status, 503);
            await unported.arrayBuffer();
            stage = "homepage";
            const homepage = await fetch(`${base}/`);
            assert.equal(homepage.status, 200);
            assert.match(await homepage.text(), /Kéire/);
            console.log(JSON.stringify({ status: "local-platform-http-smoke-passed", checks: 6,
                realAccountUsed: false, liveWebsiteChanged: false }));
        } catch {
            console.error(JSON.stringify({ status: "isolated-http-smoke-failed", stage }));
            process.exitCode = 1;
        } finally {
            await close();
        }
    }
});
