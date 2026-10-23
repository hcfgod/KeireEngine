import assert from "node:assert/strict";
import http from "node:http";
import { execFile, spawn } from "node:child_process";
import { once } from "node:events";
import { promisify } from "node:util";
import { mkdtemp, readFile, writeFile, unlink, rmdir } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const execute = promisify(execFile);
const binary = process.env.KEIRE_QA_CADDY;
assert.ok(binary && path.isAbsolute(binary), "Set KEIRE_QA_CADDY to the installed executable.");
const directory = await mkdtemp(path.join(os.tmpdir(), "keire-proxy-qa-"));
const configurationPath = path.join(directory, "caddy.json");
const logPath = path.join(directory, "access.json");
const template = fileURLToPath(new URL("../../Deployment/Caddyfile.example", import.meta.url));
const upstream = http.createServer(async (request, response) => {
    try {
        let bytes = 0;
        for await (const chunk of request) {
            bytes += chunk.length;
        }
        response.writeHead(200, { "content-type": "application/json", "x-test-private": "response-secret-marker" });
        response.end(JSON.stringify({ bytes, client: request.headers["x-keire-client-ip"],
            host: request.headers.host, protocol: request.headers["x-forwarded-proto"] }));
    } catch {
        response.writeHead(413);
        response.end();
    }
});
let child;
try {
    upstream.listen(0, "127.0.0.1");
    await once(upstream, "listening");
    const upstreamPort = upstream.address().port;
    const reservation = http.createServer();
    reservation.listen(0, "127.0.0.1");
    await once(reservation, "listening");
    const port = reservation.address().port;
    await new Promise((resolve) => reservation.close(resolve));
    const adapted = await execute(binary, ["adapt", "--config", template, "--adapter", "caddyfile"],
        { windowsHide: true, maxBuffer: 1024 * 1024,
            env: { ...process.env, KEIRE_DISTRIBUTION_HOST: "distribution.example.invalid" } });
    const configuration = JSON.parse(adapted.stdout);
    configuration.admin = { disabled: true };
    delete configuration.apps.tls;
    for (const server of Object.values(configuration.apps.http.servers)) {
        server.listen = [`127.0.0.1:${port}`];
        server.automatic_https = { disable: true };
        delete server.tls_connection_policies;
    }
    function isolate(value) {
        if (!value || typeof value !== "object") {
            return;
        }
        if (value.handler === "reverse_proxy") {
            value.upstreams = [{ dial: `127.0.0.1:${upstreamPort}` }];
            delete value.health_checks;
        }
        if (value.output === "file") {
            value.filename = logPath;
        }
        for (const item of Object.values(value)) {
            isolate(item);
        }
    }
    isolate(configuration);
    await writeFile(configurationPath, JSON.stringify(configuration), { flag: "wx" });
    child = spawn(binary, ["run", "--config", configurationPath],
        { windowsHide: true, stdio: "ignore", env: { ...process.env, XDG_CONFIG_HOME: directory, XDG_DATA_HOME: directory } });
    const ended = once(child, "exit");
    const request = (route, options = {}) => new Promise((resolve, reject) => {
        const outgoing = http.request({ hostname: "127.0.0.1", port, path: route, method: options.method ?? "GET",
            headers: { host: "distribution.example.invalid", "x-keire-client-ip": "198.51.100.99",
                "x-forwarded-proto": "http", "x-test-private": "request-secret-marker", ...options.headers },
            signal: AbortSignal.timeout(15000) }, async (response) => {
            try {
                const chunks = [];
                for await (const chunk of response) {
                    chunks.push(chunk);
                }
                resolve(new Response(Buffer.concat(chunks), { status: response.statusCode }));
            } catch (error) {
                reject(error);
            }
        });
        outgoing.on("error", reject);
        outgoing.end(options.body);
    });
    let ready = false;
    for (let attempt = 0; attempt < 50 && child.exitCode === null; attempt++) {
        try {
            const probe = await request("/probe");
            await probe.arrayBuffer();
            ready = probe.status === 200;
            if (ready) {
                break;
            }
        } catch {
            // The isolated child may not have bound its listener yet.
        }
        await new Promise((resolve) => setTimeout(resolve, 100));
    }
    assert.ok(ready, "Isolated proxy failed to become ready.");
    const accepted = await request("/publisher/v1/transfer/fixture?signature=url-secret-marker", {
        method: "PATCH", body: Buffer.alloc(6 * 1024 * 1024),
    });
    assert.equal(accepted.status, 200);
    assert.deepEqual(await accepted.json(), { bytes: 6 * 1024 * 1024, client: "127.0.0.1",
        host: "distribution.example.invalid", protocol: "https" });
    for (const [route, method, size] of [
        ["/publisher/v1/transfer/fixture", "PATCH", 8 * 1024 * 1024 + 1],
        ["/account/profile/", "POST", 3 * 1024 * 1024],
        ["/publisher/v1/transfer/fixture", "POST", 3 * 1024 * 1024],
    ]) {
        const denied = await request(route, { method, body: Buffer.alloc(size) });
        assert.equal(denied.status, 413);
        await denied.arrayBuffer();
    }
    child.kill();
    await ended;
    const logs = await readFile(logPath, "utf8");
    assert.ok(logs.trim().split("\n").length >= 5);
    for (const secret of ["url-secret-marker", "request-secret-marker", "response-secret-marker"]) {
        assert.equal(logs.includes(secret), false);
    }
    for (const line of logs.trim().split("\n")) {
        const record = JSON.parse(line);
        assert.equal(record.request.uri, undefined);
        assert.equal(record.request.headers, undefined);
        assert.equal(record.resp_headers, undefined);
    }
    console.log(JSON.stringify({ status: "proxy-acceptance-passed", liveProxyChanged: false }));
} finally {
    if (child && child.exitCode === null && child.signalCode === null) {
        const ended = once(child, "exit");
        child.kill();
        await ended;
    }
    upstream.closeAllConnections();
    await new Promise((resolve) => upstream.close(resolve));
    // Only files created by this fixture are removed. Unexpected contents remain for inspection.
    for (const file of [configurationPath, logPath]) {
        await unlink(file).catch((error) => {
            if (error.code !== "ENOENT") {
                throw error;
            }
        });
    }
    await rmdir(directory).catch((error) => {
        if (error.code !== "ENOTEMPTY") {
            throw error;
        }
    });
}
