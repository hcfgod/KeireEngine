import { readFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { productionConfiguration, createProductionBackend } from "./platform-production-backend.mjs";
import { startProductionServer } from "./platform-production-server.mjs";

// Explicit entry point only. Configuration contains paths/public settings, never raw credentials.
let backend;
let host;
try {
    const configurationPath = process.argv[2];
    if (process.argv.length !== 3 || !configurationPath || !path.isAbsolute(configurationPath)) {
        throw new Error("An absolute production configuration file is required.");
    }
    const content = await readFile(configurationPath);
    if (content.length > 32768) {
        throw new Error("Production configuration is too large.");
    }
    const configuration = productionConfiguration(JSON.parse(content.toString("utf8").replace(/^\uFEFF/, "")));
    process.env.NODE_ENV = "production";
    process.env.ASTRO_NODE_AUTOSTART = "disabled";
    process.env.KEIRE_PLATFORM_BACKEND = "mysql";
    process.env.PUBLIC_SITE_URL = configuration.siteOrigin;
    const entry = await import(pathToFileURL(path.join(configuration.webRoot, "dist/server/entry.mjs")).href);
    if (entry.options.mode !== "middleware") {
        throw new Error("Production MySQL requires a middleware build.");
    }
    backend = await createProductionBackend(configuration);
    host = await startProductionServer({ backend, entry, configuration });
    const close = () => {
        void host.close().catch(() => {
            console.error("Production website shutdown failed; protected diagnostics suppressed.");
            process.exitCode = 1;
        });
    };
    process.once("SIGINT", close);
    process.once("SIGTERM", close);
    host.server.on("error", close);
    console.log(JSON.stringify({ status: "production-platform-listening", host: "127.0.0.1", port: configuration.port }));
} catch {
    await host?.close().catch(() => {});
    await backend?.close().catch(() => {});
    console.error("Production MySQL website startup failed; inspect deployment configuration and protected settings.");
    process.exitCode = 1;
}
