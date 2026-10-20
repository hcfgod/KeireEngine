import { createPlatformPool } from "./platform-mysql.mjs";
import { LocalPasswordAuthentication } from "./platform-local-auth.mjs";
import { LocalEmailAuthentication } from "./platform-email-auth.mjs";
import { RecoveryDeliveryWorker } from "./platform-recovery-worker.mjs";
import { processRecoveryDelivery } from "./platform-recovery-delivery.mjs";
import { processRegistrationDelivery } from "./platform-registration-delivery.mjs";

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
        createAuthentication = LocalPasswordAuthentication.create, createMailer = null, createUploads = null,
        createRecoveryWorker = (processNext) => new RecoveryDeliveryWorker(processNext),
        createRegistrationWorker = (processNext) => new RecoveryDeliveryWorker(processNext) }) {
        this.provider = selectPlatformBackend(provider);
        if (this.provider === "mysql" && typeof loadLocalSecrets !== "function") {
            throw new Error("MySQL requires a protected local secrets loader.");
        }
        this.loadLocalSecrets = loadLocalSecrets;
        this.createPool = createPool;
        this.createAuthentication = createAuthentication;
        if (createMailer !== null && typeof createMailer !== "function") {
            throw new Error("Email delivery requires an application-owned factory.");
        }
        this.createMailer = createMailer;
        if (createUploads !== null && typeof createUploads !== "function") {
            throw new Error("Upload storage requires an application-owned factory.");
        }
        this.createUploads = createUploads;
        this.createRecoveryWorker = createRecoveryWorker;
        this.createRegistrationWorker = createRegistrationWorker;
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
        let mailer;
        let recoveryWorker;
        let registrationWorker;
        let uploads;
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
            if (this.createUploads) {
                uploads = await this.createUploads({ authentication, pool });
                if (!uploads || typeof uploads.close !== "function" || typeof uploads.handle !== "function"
                    || typeof uploads.reserve !== "function" || typeof uploads.prepare !== "function"
                    || typeof uploads.complete !== "function") {
                    throw new Error("Upload factory returned an invalid service.");
                }
            }
            if (this.createMailer) {
                mailer = await this.createMailer();
                if (!mailer || typeof mailer.sendVerification !== "function" || typeof mailer.close !== "function") {
                    throw new Error("Email delivery factory returned an invalid transport.");
                }
            }
            const emailAuthentication = mailer ? new LocalEmailAuthentication(authentication, mailer) : null;
            if (mailer?.recoveryEnabled === true) {
                if (typeof mailer.sendRecovery !== "function") {
                    throw new Error("Recovery delivery requires a configured recovery mail transport.");
                }
                recoveryWorker = this.createRecoveryWorker(() => processRecoveryDelivery(authentication, mailer));
                recoveryWorker.start();
            }
            if (mailer?.registrationEnabled === true) {
                if (typeof mailer.sendRegistration !== "function") {
                    throw new Error("Registration delivery requires a configured registration mail transport.");
                }
                registrationWorker = this.createRegistrationWorker(() => processRegistrationDelivery(authentication, mailer));
                registrationWorker.start();
            }
            return Object.freeze({ provider: "mysql", pool, authentication, mailer, emailAuthentication, uploads: uploads ?? null,
                registrationWorker: registrationWorker ?? null,
                recoveryWorker: recoveryWorker ?? null });
        } catch (error) {
            try {
                await uploads?.close?.();
            } catch {
                // Dispose remaining services even when upload shutdown fails.
            }
            try {
                await registrationWorker?.close();
            } catch {
                // Dispose the remaining services even when registration startup or shutdown failed.
            }
            try {
                await recoveryWorker?.close();
            } catch {
                // Continue disposing borrowed services after partial worker initialization.
            }
            try {
                await mailer?.close?.();
            } catch {
                // Continue disposing other services after partial mailer initialization.
            }
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
            try {
                try {
                    await backend?.uploads?.close();
                } finally {
                    await backend?.registrationWorker?.close();
                }
            } finally {
                await backend?.recoveryWorker?.close();
            }
        } finally {
            try {
                await backend?.mailer?.close();
            } finally {
                try {
                    backend?.authentication?.close();
                } finally {
                    await backend?.pool?.end();
                }
            }
        }
    }
}
