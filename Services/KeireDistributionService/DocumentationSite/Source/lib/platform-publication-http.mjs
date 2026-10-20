import { createHash, timingSafeEqual } from "node:crypto";
import { readObject } from "./platform-auth-http.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

const maximumRequestBytes = 512 * 1024;
const queuePath = "/internal/publication/v1/queue/";

function validInput(input) {
    if (!["lease", "renew", "complete", "fail"].includes(input.action)
        || typeof input.workerId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(input.workerId)
        || !Number.isInteger(input.leaseSeconds) || input.leaseSeconds < 60 || input.leaseSeconds > 900) {
        return false;
    }
    if (input.action === "lease") {
        return typeof input.signingKeyId === "string"
            && /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(input.signingKeyId);
    }
    if (typeof input.jobId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(input.jobId)) {
        return false;
    }
    if (input.action === "complete") {
        return typeof input.signedManifest === "string" && Buffer.byteLength(input.signedManifest) <= 65536;
    }
    return input.action !== "fail"
        || (typeof input.errorCode === "string" && /^[a-z][a-z0-9._-]{2,127}$/.test(input.errorCode)
            && typeof input.retryable === "boolean");
}

function leaseJson(lease) {
    if (lease === null) {
        return '{"data":{"lease":null}}';
    }
    const integerLimits = {
        publication_sequence: 9223372036854775807n,
        artifact_size_bytes: 64n * 1024n * 1024n * 1024n,
        evidence_size_bytes: 8n * 1024n * 1024n,
    };
    const integers = {};
    for (const [field, maximum] of Object.entries(integerLimits)) {
        const value = lease[field];
        if ((typeof value !== "string" && typeof value !== "bigint"
            && !(typeof value === "number" && Number.isSafeInteger(value)))
            || !/^[1-9][0-9]*$/.test(String(value)) || BigInt(value) > maximum) {
            throw new Error("Invalid publication lease integer.");
        }
        integers[field] = String(value);
    }
    // Only validated decimal integer tokens bypass JSON.stringify; all names and other values remain escaped.
    const fields = Object.entries(lease).map(([field, value]) =>
        `${JSON.stringify(field)}:${Object.hasOwn(integers, field) ? integers[field] : JSON.stringify(value)}`);
    return `{"data":{"lease":{${fields.join(",")}}}}`;
}

/** Host-owned boundary. Configure only with a complete service, including signed publication verification. */
export function createPublicationQueueHttp({ service, queueSecret, siteOrigin }) {
    if (!service || ["lease", "renew", "complete", "fail"].some((operation) => typeof service[operation] !== "function")) {
        throw new Error("The publication queue service is incomplete.");
    }
    if (typeof queueSecret !== "string" || !/^[\x21-\x7e]{32,256}$/.test(queueSecret)) {
        throw new Error("A dedicated publication broker credential is required.");
    }
    const configured = new URL(siteOrigin);
    if (configured.protocol !== "https:" || configured.origin !== siteOrigin) {
        throw new Error("The publication queue requires a canonical HTTPS origin.");
    }
    const expectedDigest = createHash("sha256").update(queueSecret).digest();
    queueSecret = null;
    let closed = false;
    return Object.freeze({
        close() {
            closed = true;
            expectedDigest.fill(0);
        },
        async handle(request) {
            const url = new URL(request.url);
            if (url.pathname !== queuePath) {
                return null;
            }
            const headers = new Headers({
                "content-type": "application/json; charset=utf-8",
                "cache-control": "private, no-store",
                "x-content-type-options": "nosniff",
            });
            const respond = (body, status = 200) => new Response(JSON.stringify(body), { status, headers });
            const fail = (status, code, message) => respond({ error: { code, message } }, status);
            if (closed) {
                return fail(503, "publication.unavailable", "The publication queue is unavailable.");
            }
            if (request.method !== "POST") {
                headers.set("allow", "POST");
                return fail(405, "request.method_not_allowed", "Use POST for publication operations.");
            }
            if (url.origin !== siteOrigin || url.search || request.headers.has("origin")
                || request.headers.has("cookie") || request.headers.has("authorization")
                || request.headers.has("apikey")) {
                return fail(403, "request.credentials_rejected", "Use the dedicated publication broker connection.");
            }
            const provided = request.headers.get("x-keire-publication-secret") ?? "";
            if (!/^[\x21-\x7e]{32,256}$/.test(provided)) {
                return fail(401, "publication.authentication_failed", "Authentication failed.");
            }
            const providedDigest = createHash("sha256").update(provided).digest();
            const authenticated = timingSafeEqual(expectedDigest, providedDigest);
            providedDigest.fill(0);
            if (!authenticated) {
                return fail(401, "publication.authentication_failed", "Authentication failed.");
            }
            let input;
            try {
                input = await readObject(request, maximumRequestBytes);
                if (!validInput(input)) {
                    throw new Error("Invalid request.");
                }
            } catch {
                return fail(400, "request.invalid_body", "The publication request is invalid or too large.");
            }
            try {
                if (input.action === "lease") {
                    return new Response(leaseJson(await service.lease(
                        input.workerId, input.signingKeyId, input.leaseSeconds)), { status: 200, headers });
                }
                if (input.action === "renew") {
                    return respond({ data: await service.renew(input.jobId, input.workerId, input.leaseSeconds) });
                }
                if (input.action === "fail") {
                    return respond({ data: await service.fail(input.jobId, input.workerId, input.errorCode, input.retryable) });
                }
                return respond({ data: await service.complete(input.jobId, input.workerId, input.signedManifest) });
            } catch (error) {
                if (error instanceof LocalUploadError) {
                    return fail(error.status, error.code, error.message);
                }
                return fail(503, "publication.operation_unconfirmed",
                    "The publication operation could not be confirmed.");
            }
        },
    });
}
