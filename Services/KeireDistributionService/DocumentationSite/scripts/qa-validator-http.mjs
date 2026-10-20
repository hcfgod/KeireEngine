import https from "node:https";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createLocalValidatorService } from "../Source/lib/platform-validator-service.mjs";
import { handleLocalPlatformRequest } from "../Source/lib/platform-request.mjs";

/** Only accepts the caller's isolated database fixture and stores; never opens the real queue itself. */
export async function createQaValidatorHttp({ pool, packageStore, evidenceStore, brokerSecret, transferKey }) {
    return createQaPlatformHttp("validator", (siteOrigin) => createLocalValidatorService({
        pool, packageStore, evidenceStore, brokerSecret, transferKey, siteOrigin,
    }));
}

export async function createQaPlatformHttp(serviceName, createService) {
    if (!["validator", "publication"].includes(serviceName) || typeof createService !== "function") {
        throw new Error("Invalid isolated queue fixture.");
    }
    const thumbprint = process.env.KEIRE_QA_TLS_THUMBPRINT;
    const powershell = process.env.KEIRE_QA_POWERSHELL;
    if (!/^[a-fA-F0-9]{40}$/.test(thumbprint ?? "") || !path.isAbsolute(powershell ?? "")) {
        throw new Error("Validator HTTPS acceptance requires protected localhost TLS configuration.");
    }
    const script = fileURLToPath(new URL("../../scripts/read-local-platform-qa-tls.ps1", import.meta.url));
    let tls;
    try {
        const result = await promisify(execFile)(powershell, [
            "-NoProfile", "-NonInteractive", "-File", script, "-Thumbprint", thumbprint,
        ], { windowsHide: true, shell: false, timeout: 15000, maxBuffer: 65536 });
        const payload = JSON.parse(result.stdout.replace(/^\uFEFF/, ""));
        tls = { pfx: Buffer.from(payload.pfx, "base64"), passphrase: payload.passphrase, minVersion: "TLSv1.2" };
    } catch {
        throw new Error("Could not load protected QA TLS configuration.");
    }
    let service;
    let origin;
    const server = https.createServer(tls, async (incoming, outgoing) => {
        try {
            const options = { method: incoming.method, headers: incoming.headers };
            if (!["GET", "HEAD"].includes(incoming.method)) {
                options.body = Readable.toWeb(incoming);
                options.duplex = "half";
            }
            const request = new Request(new URL(incoming.url, origin), options);
            const response = await handleLocalPlatformRequest({
                backend: { provider: "mysql", withBackend: (action) => action({ [serviceName]: service }) },
                request, siteOrigin: origin, correlationId: "isolated-validator-acceptance",
                render: () => { throw new Error("Validator requests must not render browser pages."); },
            });
            outgoing.writeHead(response.status, Object.fromEntries(response.headers));
            if (response.body) {
                await pipeline(Readable.fromWeb(response.body), outgoing);
            } else {
                outgoing.end();
            }
        } catch {
            if (!outgoing.headersSent) {
                outgoing.writeHead(500);
            }
            outgoing.end();
        }
    });
    tls.pfx.fill(0);
    tls = null;
    try {
        await new Promise((resolve, reject) => {
            server.once("error", reject);
            server.listen(0, "127.0.0.1", resolve);
        });
        origin = `https://localhost:${server.address().port}`;
        service = createService(origin);
    } catch (error) {
        server.close();
        throw error;
    }
    return {
        origin,
        async close() {
            await new Promise((resolve, reject) => {
                server.close((error) => error ? reject(error) : resolve());
                server.closeIdleConnections();
            });
            await service.close();
        },
    };
}
