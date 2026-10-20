import { createValidatorCapabilities } from "./platform-validator-capabilities.mjs";
import { completeValidationUpload } from "./platform-validator-complete.mjs";
import { createValidatorFileHttp } from "./platform-validator-files.mjs";
import { createValidatorQueueHttp } from "./platform-validator-http.mjs";
import { leaseValidationUpload, renewValidationLease } from "./platform-validator-leases.mjs";

/** Borrows verified stores and the pool. The host owns their configuration and lifetime. */
export function createLocalValidatorService({ pool, packageStore, evidenceStore,
    siteOrigin, brokerSecret, transferKey }) {
    if (typeof pool?.getConnection !== "function" || typeof packageStore?.read !== "function"
        || typeof evidenceStore?.put !== "function" || typeof evidenceStore?.verify !== "function") {
        throw new Error("The validator requires a database and verified package/evidence stores.");
    }
    const capabilities = createValidatorCapabilities(transferKey, siteOrigin);
    let queue;
    try {
        queue = createValidatorQueueHttp({
            siteOrigin,
            brokerSecret,
            service: {
                async lease(workerId, leaseSeconds) {
                    const lease = await leaseValidationUpload(pool, workerId, leaseSeconds);
                    if (!lease) {
                        return null;
                    }
                    // Attempt is internal authorization state, not part of the strict C# wire contract.
                    const { validationAttempt, ...response } = lease;
                    const claims = { uploadId: lease.uploadId, workerId, attempt: validationAttempt };
                    return {
                        ...response,
                        downloadUrl: capabilities.issue({ ...claims, purpose: "package" }),
                        evidenceUploadUrl: capabilities.issue({ ...claims, purpose: "evidence" }),
                    };
                },
                renew: (uploadId, workerId, leaseSeconds) =>
                    renewValidationLease(pool, uploadId, workerId, leaseSeconds),
                complete: (uploadId, workerId, report) =>
                    completeValidationUpload(pool, evidenceStore, uploadId, workerId, report),
            },
        });
    } catch (error) {
        capabilities.close();
        throw error;
    }
    const files = createValidatorFileHttp({ capabilities, pool, packageStore, evidenceStore });
    let closing = false;
    let active = 0;
    let drained;
    let shutdown;

    return Object.freeze({
        async handle(request) {
            const pathname = new URL(request.url).pathname;
            if (pathname !== "/internal/validator/v1/queue/"
                && !pathname.startsWith("/internal/validator/v1/files/")) {
                return null;
            }
            if (closing) {
                return Response.json({ error: { code: "validator.unavailable",
                    message: "The validator service is unavailable." } },
                { status: 503, headers: { "cache-control": "private, no-store" } });
            }
            active++;
            try {
                return await queue.handle(request) ?? await files(request);
            } finally {
                active--;
                if (active === 0) {
                    drained?.();
                }
            }
        },
        close() {
            closing = true;
            shutdown ??= (async () => {
                if (active !== 0) {
                    await new Promise((resolve) => { drained = resolve; });
                }
                queue.close();
                capabilities.close();
            })();
            return shutdown;
        },
    });
}
