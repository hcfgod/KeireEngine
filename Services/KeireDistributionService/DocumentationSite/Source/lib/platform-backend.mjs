import { createPlatformPool } from "./platform-mysql.mjs";
import { LocalPasswordAuthentication } from "./platform-local-auth.mjs";

export function selectPlatformBackend(value) {
    if (value === undefined || value === "supabase") {
        return "supabase";
    }
    if (value === "mysql") {
        return "mysql";
    }
    throw new Error("Platform backend must be supabase or mysql.");
}

/** Owned by the server application, not a process-global singleton or a request. */
export class PlatformBackend {
    constructor({ provider, loadLocalSecrets, createPool = createPlatformPool,
        createAuthentication = LocalPasswordAuthentication.create }) {
        this.provider = selectPlatformBackend(provider);
        if (this.provider === "mysql" && typeof loadLocalSecrets !== "function") {
            throw new Error("MySQL requires a protected local secrets loader.");
        }
        this.loadLocalSecrets = loadLocalSecrets;
        this.createPool = createPool;
        this.createAuthentication = createAuthentication;
        this.activeRequests = 0;
        this.closing = false;
        this.initialization = null;
        this.shutdown = null;
    }

    async initialize() {
        if (this.provider === "supabase") {
            // Supabase cookie clients remain request-owned; never share their mutable session state.
            return Object.freeze({ provider: "supabase" });
        }
        let pool;
        let authentication;
        let secrets;
        try {
            // Loader transfers ownership of its fresh key buffer to this initialization operation.
            secrets = await this.loadLocalSecrets();
            if (!Buffer.isBuffer(secrets?.accountEncryptionKey) || secrets.accountEncryptionKey.length !== 32) {
                throw new Error("Protected account encryption key is invalid.");
            }
            pool = await this.createPool(secrets.settings, "runtime");
            await pool.query("SELECT 1");
            authentication = await this.createAuthentication(pool, secrets.accountEncryptionKey);
            return Object.freeze({ provider: "mysql", pool, authentication });
        } catch (error) {
            try {
                authentication?.close();
            } catch {
                // Continue disposing the pool after a partial authentication initialization failure.
            }
            try {
                await pool?.end();
            } catch {
                // Preserve the initialization failure; there is no fallback to another identity provider.
            }
            throw error;
        } finally {
            if (Buffer.isBuffer(secrets?.accountEncryptionKey)) {
                secrets.accountEncryptionKey.fill(0);
            }
            secrets = null;
        }
    }

    async withBackend(action) {
        if (this.closing) {
            throw new Error("Platform backend is shutting down.");
        }
        this.activeRequests++;
        try {
            // Retain failures too: repeated requests must not launch connection/password-hash storms.
            this.initialization ??= this.initialize();
            return await action(await this.initialization);
        } finally {
            this.activeRequests--;
            if (this.activeRequests === 0) {
                this.drained?.();
            }
        }
    }

    close() {
        if (this.shutdown) {
            return this.shutdown;
        }
        this.closing = true;
        this.shutdown = this.finishShutdown();
        return this.shutdown;
    }

    async finishShutdown() {
        if (this.activeRequests > 0) {
            await new Promise((resolve) => {
                this.drained = resolve;
            });
        }
        let backend;
        try {
            backend = await this.initialization;
        } catch {
            // Failed initialization already disposed its partially created resources.
            return;
        }
        try {
            backend?.authentication?.close();
        } finally {
            await backend?.pool?.end();
        }
    }
}
