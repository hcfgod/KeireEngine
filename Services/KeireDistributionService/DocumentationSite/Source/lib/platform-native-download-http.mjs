import { authorizeNativeDownloadDelivery } from "./platform-native-download-delivery.mjs";
import { LocalClaimError } from "./platform-claims.mjs";

/** Borrows the host's capability signer and immutable private blob store. */
export function createNativeDownloadHttp({ pool, clientId, capabilities, packageStore }) {
    let closing = false;
    let shutdown;
    const preparing = new Set();
    const streams = new Set();
    const handle = async (request) => {
        if (!new URL(request.url).pathname.startsWith("/marketplace/v1/downloads/")) {
            return null;
        }
        const headers = {
            "cache-control": "private, no-store",
            "x-content-type-options": "nosniff",
            "referrer-policy": "no-referrer",
        };
        const fail = (status, code, message) => Response.json({ error: { code, message } }, { status, headers });
        if (closing) {
            return fail(503, "marketplace.unavailable", "Downloads are temporarily unavailable. Please try again shortly.");
        }
        if (request.method !== "GET") {
            return new Response(null, { status: 405, headers: { ...headers, allow: "GET" } });
        }
        const capability = capabilities.verify(request.url);
        if (!capability) {
            return fail(403, "marketplace.download_expired", "Request a new download from Kéire Hub.");
        }
        let iterator;
        try {
            const release = await authorizeNativeDownloadDelivery(pool, capability.grantId, clientId);
            const sizeBytes = Number(release.archive_size_bytes);
            if (!Number.isSafeInteger(sizeBytes) || sizeBytes <= 0 || sizeBytes > 64 * 1024 ** 3
                || !/^[a-f0-9]{64}$/.test(release.archive_sha256)) {
                throw new Error("Invalid package identity.");
            }
            iterator = packageStore.read({ sha256: release.archive_sha256, sizeBytes })[Symbol.asyncIterator]();
            // LocalBlobStore verifies the whole file before yielding and retains that same handle.
            const first = await iterator.next();
            if (first.done || request.signal.aborted || !capabilities.verify(request.url)) {
                throw new LocalClaimError(403, "marketplace.download_expired", "Request a new download from Kéire Hub.");
            }
            await authorizeNativeDownloadDelivery(pool, capability.grantId, clientId);
            if (closing) {
                throw new Error("Download service is closing.");
            }
            let pending = first;
            let finished = false;
            let cleanup;
            let abort;
            const stop = async () => {
                abort();
                await dispose();
            };
            const dispose = () => {
                finished = true;
                request.signal.removeEventListener("abort", abort);
                cleanup ??= Promise.resolve().then(() => iterator.return?.()).finally(() => streams.delete(stop));
                return cleanup;
            };
            const stream = new ReadableStream({
                start(controller) {
                    abort = () => {
                        if (!finished) {
                            controller.error(new Error("Download cancelled."));
                            // Abort events have no promise consumer; cleanup is still awaited by cancel/pull.
                            void dispose().catch(() => {});
                        }
                    };
                    request.signal.addEventListener("abort", abort, { once: true });
                    streams.add(stop);
                    if (request.signal.aborted) {
                        abort();
                    }
                },
                async pull(controller) {
                    try {
                        if (finished) {
                            return;
                        }
                        if (request.signal.aborted) {
                            throw new Error("Download cancelled.");
                        }
                        const result = pending ?? await iterator.next();
                        pending = null;
                        if (finished) {
                            return;
                        }
                        if (result.done) {
                            await dispose();
                            controller.close();
                        } else {
                            controller.enqueue(result.value);
                        }
                    } catch (error) {
                        try {
                            await dispose();
                        } finally {
                            controller.error(error);
                        }
                    }
                },
                async cancel() {
                    await dispose();
                },
            });
            return new Response(stream, { headers: {
                ...headers,
                "content-type": "application/octet-stream",
                "content-length": String(sizeBytes),
                "content-disposition": 'attachment; filename="asset.keirepackage"',
            } });
        } catch (error) {
            try {
                await iterator?.return?.();
            } catch {
                // Preserve the original authorization or storage failure.
            }
            if (error instanceof LocalClaimError) {
                return fail(error.status, error.code, error.message);
            }
            return fail(503, "marketplace.package_unavailable", "This package is temporarily unavailable. Please try again shortly.");
        }
    };
    const tracked = (request) => {
        const pending = handle(request);
        preparing.add(pending);
        void pending.then(() => preparing.delete(pending), () => preparing.delete(pending));
        return pending;
    };
    tracked.close = () => {
        closing = true;
        shutdown ??= (async () => {
            await Promise.allSettled([...preparing]);
            const results = await Promise.allSettled([...streams].map((stop) => stop()));
            if (results.some((result) => result.status === "rejected")) {
                throw new Error("A download stream could not be closed cleanly.");
            }
        })();
        return shutdown;
    };
    return tracked;
}
