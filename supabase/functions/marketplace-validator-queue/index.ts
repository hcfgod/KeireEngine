import { createClient } from "npm:@supabase/supabase-js@2.112.3";

const maximumRequestBytes = 1024 * 1024;

class RequestError extends Error {
    constructor(readonly status: number, readonly code: string, message: string) {
        super(message);
        this.name = "RequestError";
    }
}

function json(status: number, body: Record<string, unknown>): Response {
    return new Response(JSON.stringify(body), {
        status,
        headers: { "Cache-Control": "no-store", "Content-Type": "application/json; charset=utf-8" },
    });
}

async function readJson(request: Request): Promise<Record<string, unknown>> {
    if (request.headers.get("content-type")?.split(";", 1)[0]?.trim().toLowerCase() !== "application/json") {
        throw new RequestError(415, "request.unsupported_media_type", "A JSON request is required.");
    }
    const declared = request.headers.get("content-length");
    if (declared && (!/^\d+$/.test(declared) || Number(declared) > maximumRequestBytes)) {
        throw new RequestError(413, "request.too_large", "The request is too large.");
    }
    if (!request.body) throw new RequestError(400, "request.invalid_json", "The request body is required.");
    const reader = request.body.getReader();
    const chunks: Uint8Array[] = [];
    let length = 0;
    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            length += value.byteLength;
            if (length > maximumRequestBytes) {
                await reader.cancel("request body exceeded its limit");
                throw new RequestError(413, "request.too_large", "The request is too large.");
            }
            chunks.push(value);
        }
    } finally {
        reader.releaseLock();
    }
    if (length === 0) throw new RequestError(400, "request.invalid_json", "The request body is required.");
    const bytes = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) {
        bytes.set(chunk, offset);
        offset += chunk.byteLength;
    }
    try {
        const value: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
        if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("not an object");
        return value as Record<string, unknown>;
    } catch {
        throw new RequestError(400, "request.invalid_json", "The request body must be a JSON object.");
    }
}

async function authenticate(request: Request): Promise<void> {
    if (request.headers.has("origin")) {
        throw new RequestError(403, "request.origin_rejected", "Browser-origin requests are not accepted.");
    }
    const expected = Deno.env.get("VALIDATOR_BROKER_SECRET") ?? "";
    const provided = request.headers.get("x-keire-validator-secret") ?? "";
    if (expected.length < 32 || expected.length > 256 || provided.length > 256) {
        throw new RequestError(503, "validator.not_configured", "The validator queue is not configured.");
    }
    const encoder = new TextEncoder();
    const [expectedDigest, providedDigest] = await Promise.all([
        crypto.subtle.digest("SHA-256", encoder.encode(expected)),
        crypto.subtle.digest("SHA-256", encoder.encode(provided)),
    ]);
    const expectedBytes = new Uint8Array(expectedDigest);
    const providedBytes = new Uint8Array(providedDigest);
    let difference = 0;
    for (let index = 0; index < expectedBytes.length; ++index) difference |= expectedBytes[index] ^ providedBytes[index];
    if (difference !== 0) throw new RequestError(401, "validator.authentication_failed", "Authentication failed.");
}

function stringField(input: Record<string, unknown>, name: string, minimum: number, maximum: number): string {
    const value = input[name];
    if (typeof value !== "string" || value.length < minimum || value.length > maximum) {
        throw new RequestError(400, "request.invalid_field", `${name} is invalid.`);
    }
    return value;
}

function integerField(input: Record<string, unknown>, name: string, minimum: number, maximum: number): number {
    const value = input[name];
    if (!Number.isSafeInteger(value) || (value as number) < minimum || (value as number) > maximum) {
        throw new RequestError(400, "request.invalid_field", `${name} is invalid.`);
    }
    return value as number;
}

function uuidField(input: Record<string, unknown>, name: string): string {
    const value = stringField(input, name, 36, 36);
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value)) {
        throw new RequestError(400, "request.invalid_field", `${name} is invalid.`);
    }
    return value;
}

function serviceKey(): string | null {
    try {
        const keys = JSON.parse(Deno.env.get("SUPABASE_SECRET_KEYS") ?? "{}") as Record<string, unknown>;
        if (typeof keys.default === "string" && keys.default) return keys.default;
    } catch {
        // The legacy service-role variable remains a supported platform fallback.
    }
    return Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? null;
}

Deno.serve(async (request: Request) => {
    try {
        if (request.method !== "POST") throw new RequestError(405, "request.method_not_allowed", "POST is required.");
        await authenticate(request);
        const url = Deno.env.get("SUPABASE_URL") ?? "";
        const key = serviceKey();
        if (!url || !key) throw new RequestError(503, "validator.not_configured", "The validator queue is not configured.");
        const admin = createClient(url, key, {
            auth: { autoRefreshToken: false, persistSession: false },
            global: { headers: { "X-Client-Info": "keire-validator-queue/0.3.1" } },
        });
        const input = await readJson(request);
        const action = stringField(input, "action", 5, 16);
        const workerId = stringField(input, "workerId", 3, 128);
        if (!/^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(workerId)) {
            throw new RequestError(400, "request.invalid_field", "workerId is invalid.");
        }
        const leaseSeconds = integerField(input, "leaseSeconds", 60, 1800);
        if (action === "lease") {
            const { data, error } = await admin.rpc("service_lease_marketplace_upload", {
                p_worker_id: workerId,
                p_lease_seconds: leaseSeconds,
            });
            if (error) throw new RequestError(503, "validator.lease_failed", "A validation lease could not be acquired.");
            const lease = Array.isArray(data) ? data[0] : null;
            if (!lease) return json(200, { data: { lease: null } });
            const { data: signed, error: signError } = await admin.storage.from("marketplace-quarantine")
                .createSignedUrl(lease.storage_path, leaseSeconds, { download: true });
            if (signError || !signed?.signedUrl) {
                throw new RequestError(503, "validator.download_grant_failed", "The quarantine download could not be granted.");
            }
            return json(200, {
                data: {
                    lease: {
                        uploadId: lease.upload_id,
                        versionId: lease.version_id,
                        storagePath: lease.storage_path,
                        expectedSizeBytes: lease.expected_size_bytes,
                        expectedSha256: lease.expected_sha256,
                        leaseExpiresAt: lease.lease_expires_at,
                        downloadUrl: signed.signedUrl,
                    },
                },
            });
        }
        if (action === "renew") {
            const { data, error } = await admin.rpc("service_renew_marketplace_upload_lease", {
                p_upload_id: uuidField(input, "uploadId"),
                p_worker_id: workerId,
                p_lease_seconds: leaseSeconds,
            });
            if (error) throw new RequestError(409, "validator.lease_lost", "The validation lease could not be renewed.");
            return json(200, { data: { leaseExpiresAt: data } });
        }
        if (action === "complete") {
            const report = input.report;
            if (!report || typeof report !== "object" || Array.isArray(report)) {
                throw new RequestError(400, "request.invalid_field", "report is invalid.");
            }
            const { data, error } = await admin.rpc("service_complete_marketplace_validation", {
                p_upload_id: uuidField(input, "uploadId"),
                p_worker_id: workerId,
                p_report: report,
            });
            if (error) throw new RequestError(409, "validator.commit_failed", "The validation report could not be committed.");
            return json(200, { data: { reportId: data } });
        }
        throw new RequestError(400, "request.action_invalid", "The validator queue action is invalid.");
    } catch (error) {
        if (error instanceof RequestError) return json(error.status, { error: { code: error.code, message: error.message } });
        console.error("validator queue failed", error instanceof Error ? error.message : String(error));
        return json(500, { error: { code: "validator.internal_error", message: "The validator queue failed." } });
    }
});
