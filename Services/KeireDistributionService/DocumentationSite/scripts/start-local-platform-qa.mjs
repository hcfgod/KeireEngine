import http from "node:http";
import assert from "node:assert/strict";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import send from "send";
import { PlatformBackend } from "../Source/lib/platform-backend.mjs";
import { loadWindowsPlatformSecrets } from "../Source/lib/platform-secrets.mjs";

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
process.env.PUBLIC_SITE_URL = "https://qa.invalid.example";
const entry = await import(pathToFileURL(path.join(root, "dist/server/entry.mjs")).href);
if (entry.options.mode !== "middleware") {
    throw new Error("Build with KEIRE_NODE_ADAPTER_MODE=middleware before starting isolated QA.");
}
const backend = new PlatformBackend({ provider: "mysql", loadLocalSecrets: () => loadWindowsPlatformSecrets({
    powerShellExecutable: process.env.KEIRE_QA_POWERSHELL,
    decryptScriptPath: path.resolve(root, "../scripts/read-local-platform-runtime.ps1"),
    settingsDirectory: process.env.KEIRE_QA_SETTINGS_DIRECTORY,
}) });
await backend.withBackend(() => undefined);

const server = http.createServer((request, response) => {
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
});
server.requestTimeout = 30000;
server.headersTimeout = 10000;
let shutdown;
function close() {
    shutdown ??= (async () => {
        await new Promise((resolve) => server.close(resolve));
        await backend.close();
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
            const base = `http://127.0.0.1:${port}`;
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
