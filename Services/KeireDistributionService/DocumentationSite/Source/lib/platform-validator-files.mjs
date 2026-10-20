import { createHash } from "node:crypto";
import { Readable } from "node:stream";
import { authorizeValidatorTransfer } from "./platform-validator-capabilities.mjs";
import { ValidatorQueueError } from "./platform-validator-error.mjs";

const maximumEvidenceBytes = 8 * 1024 * 1024;

async function evidenceBytes(request) {
    if (request.headers.get("content-type")?.split(";")[0].trim().toLowerCase() !== "application/json"
        || request.headers.get("x-upsert") === "true" || !request.body) {
        throw new ValidatorQueueError(400, "validator.evidence_invalid", "A bounded JSON evidence upload is required.");
    }
    const reader = request.body.getReader();
    const chunks = [];
    let size = 0;
    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) {
                break;
            }
            size += value.byteLength;
            if (size > maximumEvidenceBytes) {
                await reader.cancel();
                throw new ValidatorQueueError(413, "validator.evidence_too_large", "Review evidence exceeds its size limit.");
            }
            chunks.push(value);
        }
    } finally {
        reader.releaseLock();
    }
    if (size < 2) {
        throw new ValidatorQueueError(400, "validator.evidence_invalid", "Review evidence is empty.");
    }
    return Buffer.concat(chunks);
}

/** Host must redact query strings from access logs: URLs contain scoped, revocable credentials. */
export function createValidatorFileHttp({ capabilities, pool, packageStore, evidenceStore }) {
    return async function handle(request) {
        if (!new URL(request.url).pathname.startsWith("/internal/validator/v1/files/")) {
            return null;
        }
        const headers = new Headers({
            "cache-control": "private, no-store", "x-content-type-options": "nosniff",
            "referrer-policy": "no-referrer",
        });
        const json = (body, status = 200) => {
            headers.set("content-type", "application/json; charset=utf-8");
            return new Response(JSON.stringify(body), { status, headers });
        };
        try {
            if (["origin", "cookie", "authorization", "apikey"].some((name) => request.headers.has(name))) {
                throw new ValidatorQueueError(403, "validator.transfer_denied", "Use the dedicated validator file link.");
            }
            const claims = capabilities.verify(request.url, request.method);
            const upload = await authorizeValidatorTransfer(pool, claims);
            if (claims.purpose === "package") {
                const identity = { sha256: upload.expected_sha256, sizeBytes: Number(upload.expected_size_bytes) };
                const iterator = packageStore.read(identity)[Symbol.asyncIterator]();
                let first;
                try {
                    first = await iterator.next();
                    await authorizeValidatorTransfer(pool, claims);
                } catch (error) {
                    await iterator.return?.();
                    throw error;
                }
                async function* bytes() {
                    try {
                        if (!first.done) {
                            yield first.value;
                        }
                        while (true) {
                            const item = await iterator.next();
                            if (item.done) {
                                break;
                            }
                            yield item.value;
                        }
                    } finally {
                        await iterator.return?.();
                    }
                }
                headers.set("content-type", "application/octet-stream");
                headers.set("content-length", String(identity.sizeBytes));
                headers.set("content-disposition", 'attachment; filename="package.keireassetpackage"');
                return new Response(Readable.toWeb(Readable.from(bytes())), { headers });
            }
            const bytes = await evidenceBytes(request);
            await authorizeValidatorTransfer(pool, claims);
            const identity = { sha256: createHash("sha256").update(bytes).digest("hex"), sizeBytes: bytes.length };
            async function* chunks() {
                yield bytes;
            }
            await evidenceStore.put(identity, chunks());
            await authorizeValidatorTransfer(pool, claims);
            return json({ data: { uploaded: true } });
        } catch (error) {
            if (error instanceof ValidatorQueueError) {
                return json({ error: { code: error.code, message: error.message } }, error.status);
            }
            return json({ error: { code: "validator.transfer_unavailable",
                message: "The validator file transfer could not be completed." } }, 503);
        }
    };
}
