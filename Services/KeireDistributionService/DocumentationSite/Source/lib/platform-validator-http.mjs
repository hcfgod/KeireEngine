import { createHash, timingSafeEqual } from "node:crypto";
import { readObject } from "./platform-auth-http.mjs";
import { ValidatorQueueError } from "./platform-validator-error.mjs";

const maximumRequestBytes = 1024 * 1024;
const queuePath = "/internal/validator/v1/queue/";

function validInput(input) {
    if (!["lease", "renew", "complete"].includes(input.action)
        || typeof input.workerId !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(input.workerId)
        || !Number.isInteger(input.leaseSeconds) || input.leaseSeconds < 60 || input.leaseSeconds > 1800) {
        return false;
    }
    if (input.action === "lease") {
        return true;
    }
    if (typeof input.uploadId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(input.uploadId)) {
        return false;
    }
    return input.action !== "complete"
        || (input.report !== null && typeof input.report === "object" && !Array.isArray(input.report));
}

/** Host-owned boundary. Configure only with a complete service, including signed report verification. */
export function createValidatorQueueHttp({ service, brokerSecret, siteOrigin }) {
    if (!service || ["lease", "renew", "complete"].some((operation) => typeof service[operation] !== "function")) {
        throw new Error("The validator queue service is incomplete.");
    }
    if (typeof brokerSecret !== "string" || !/^[\x21-\x7e]{32,256}$/.test(brokerSecret)) {
        throw new Error("A dedicated validator broker credential is required.");
    }
    const configured = new URL(siteOrigin);
    if (configured.protocol !== "https:" || configured.origin !== siteOrigin) {
        throw new Error("The validator queue requires a canonical HTTPS origin.");
    }
    const expectedDigest = createHash("sha256").update(brokerSecret).digest();
    brokerSecret = null;
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
                return fail(503, "validator.unavailable", "The validator queue is unavailable.");
            }
            if (request.method !== "POST") {
                headers.set("allow", "POST");
                return fail(405, "request.method_not_allowed", "Use POST for validator operations.");
            }
            if (url.origin !== siteOrigin || url.search || request.headers.has("origin")
                || request.headers.has("cookie") || request.headers.has("authorization")
                || request.headers.has("apikey")) {
                return fail(403, "request.credentials_rejected", "Use the dedicated validator broker connection.");
            }
            const provided = request.headers.get("x-keire-validator-secret") ?? "";
            if (!/^[\x21-\x7e]{32,256}$/.test(provided)) {
                return fail(401, "validator.authentication_failed", "Authentication failed.");
            }
            const providedDigest = createHash("sha256").update(provided).digest();
            const authenticated = timingSafeEqual(expectedDigest, providedDigest);
            providedDigest.fill(0);
            if (!authenticated) {
                return fail(401, "validator.authentication_failed", "Authentication failed.");
            }
            let input;
            try {
                input = await readObject(request, maximumRequestBytes);
                if (!validInput(input)) {
                    throw new Error("Invalid request.");
                }
            } catch {
                return fail(400, "request.invalid_body", "The validator request is invalid or too large.");
            }
            try {
                if (input.action === "lease") {
                    return respond({ data: { lease: await service.lease(input.workerId, input.leaseSeconds) } });
                }
                if (input.action === "renew") {
                    return respond({ data: await service.renew(input.uploadId, input.workerId, input.leaseSeconds) });
                }
                return respond({ data: await service.complete(input.uploadId, input.workerId, input.report) });
            } catch (error) {
                if (error instanceof ValidatorQueueError) {
                    return fail(error.status, error.code, error.message);
                }
                return fail(503, "validator.operation_unconfirmed",
                    "The validator operation could not be confirmed.");
            }
        },
    });
}
