import http from "node:http";
import path from "node:path";
import send from "send";
import { createProductionProxyPolicy } from "./platform-production-proxy.mjs";
import { isNativeAuthorizationPath } from "../Source/lib/platform-native-service.mjs";

/** All forwarded identity is validated before either static delivery or Astro receives the request. */
export function createProductionListener({ backend, entry, configuration, serveStatic = send }) {
    const policy = createProductionProxyPolicy(configuration.siteOrigin);
    return (request, response) => {
        const fail = (status, message) => {
            if (!response.headersSent) {
                response.writeHead(status, { "cache-control": "private, no-store", "content-type": "text/plain; charset=utf-8",
                    "x-content-type-options": "nosniff" });
            }
            response.end(message);
        };
        let clientIdentity;
        try {
            if (policy.healthRequest(request)) {
                void backend.withBackend(async ({ pool }) => {
                    await pool.query("SELECT 1");
                    response.writeHead(204, { "cache-control": "no-store" });
                    response.end();
                }).catch(() => fail(503, "The platform is not ready."));
                return;
            }
            clientIdentity = policy.authorize(request);
        } catch {
            fail(400, "The request could not be verified by the website proxy.");
            return;
        }
        const serveApplication = async () => {
            try {
                await backend.withBackend(async () => {
                    const finished = new Promise((resolve) => {
                        const complete = () => {
                            response.off("finish", complete);
                            response.off("close", complete);
                            resolve();
                        };
                        response.once("finish", complete);
                        response.once("close", complete);
                    });
                    await entry.handler(request, response, () => fail(404, "Not found."), {
                        platformBackend: backend, platformClientIdentity: clientIdentity,
                    });
                    await finished;
                });
            } catch {
                fail(503, "The platform is temporarily unavailable.");
            }
        };
        const pathname = new URL(request.url, configuration.siteOrigin).pathname;
        if (!["GET", "HEAD"].includes(request.method) || isNativeAuthorizationPath(pathname)) {
            void serveApplication();
            return;
        }
        serveStatic(request, pathname, { root: path.join(configuration.webRoot, "dist/client"), dotfiles: "deny", index: ["index.html"] })
            .on("error", (error) => {
                if (error.status === 404) {
                    void serveApplication();
                } else {
                    fail(error.status === 403 ? 403 : 400, "Static resource unavailable.");
                }
            }).pipe(response);
    };
}

/** Listener is loopback-only. Stop accepting connections, drain responses, then release owned services. */
export async function startProductionServer({ backend, entry, configuration }) {
    if (entry?.options?.mode !== "middleware" || typeof entry.handler !== "function") {
        throw new Error("Production MySQL requires the middleware website build.");
    }
    const server = http.createServer(createProductionListener({ backend, entry, configuration }));
    server.requestTimeout = 30000;
    server.headersTimeout = 10000;
    server.keepAliveTimeout = 5000;
    const unstarted = new Set();
    let closing = false;
    let shutdown;
    server.on("connection", (socket) => {
        if (closing) {
            socket.destroy();
            return;
        }
        unstarted.add(socket);
        socket.once("close", () => unstarted.delete(socket));
    });
    server.prependListener("request", (request) => unstarted.delete(request.socket));
    const close = () => {
        if (!shutdown) {
            closing = true;
            shutdown = (async () => {
                try {
                    await new Promise((resolve, reject) => {
                        server.close((error) => error ? reject(error) : resolve());
                        for (const socket of unstarted) {
                            socket.destroy();
                        }
                        unstarted.clear();
                    });
                } finally {
                    await backend.close();
                }
            })();
        }
        return shutdown;
    };
    try {
        await new Promise((resolve, reject) => {
            const failed = (error) => {
                server.off("listening", ready);
                reject(error);
            };
            const ready = () => {
                server.off("error", failed);
                resolve();
            };
            server.once("error", failed);
            server.once("listening", ready);
            server.listen(configuration.port, "127.0.0.1");
        });
    } catch {
        server.close();
        await backend.close().catch(() => {});
        throw new Error("Production website listener could not start.");
    }
    return { close, address: server.address(), server };
}
