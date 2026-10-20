import { completeLocalPublication } from "./platform-publication-complete.mjs";
import { createPublicationQueueHttp } from "./platform-publication-http.mjs";
import { failLocalPublication, leaseLocalPublication, renewLocalPublication } from "./platform-publication-leases.mjs";

/** Borrows the pool and immutable stores; the host must close this service before closing those dependencies. */
export function createLocalPublicationService({ pool, packageStore, evidenceStore, siteOrigin, queueSecret }) {
    if (typeof pool?.getConnection !== "function" || typeof packageStore?.verify !== "function"
        || typeof evidenceStore?.verify !== "function") {
        throw new Error("Publication requires a database and verified immutable package/evidence stores.");
    }
    const queue = createPublicationQueueHttp({
        siteOrigin,
        queueSecret,
        service: {
            lease: (workerId, signingKeyId, leaseSeconds) =>
                leaseLocalPublication(pool, workerId, signingKeyId, leaseSeconds),
            renew: (jobId, workerId, leaseSeconds) => renewLocalPublication(pool, jobId, workerId, leaseSeconds),
            complete: (jobId, workerId, signedManifest) =>
                completeLocalPublication(pool, { packageStore, evidenceStore }, jobId, workerId, signedManifest),
            fail: (jobId, workerId, errorCode, retryable) =>
                failLocalPublication(pool, jobId, workerId, errorCode, retryable),
        },
    });
    let closing = false;
    let active = 0;
    let drained;
    let shutdown;
    return Object.freeze({
        async handle(request) {
            if (new URL(request.url).pathname !== "/internal/publication/v1/queue/") {
                return null;
            }
            if (closing) {
                return Response.json({ error: { code: "publication.unavailable",
                    message: "The publication service is unavailable." } },
                { status: 503, headers: { "cache-control": "private, no-store" } });
            }
            active++;
            try {
                return await queue.handle(request);
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
            })();
            return shutdown;
        },
    });
}
