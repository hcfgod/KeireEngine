// deno-lint-ignore no-import-prefix -- Supabase Edge Functions resolve their pinned npm dependency directly.
import { createClient, type SupabaseClient } from "npm:@supabase/supabase-js@2.112.3";

const maximumRequestBytes = 96 * 1024;

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
    const bytes = new Uint8Array(await request.arrayBuffer());
    if (bytes.byteLength === 0 || bytes.byteLength > maximumRequestBytes) {
        throw new RequestError(400, "request.invalid_json", "A bounded JSON object is required.");
    }
    try {
        const value: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
        if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("object");
        return value as Record<string, unknown>;
    } catch {
        throw new RequestError(400, "request.invalid_json", "The request body must be a JSON object.");
    }
}

async function authenticate(request: Request): Promise<void> {
    if (request.headers.has("origin")) {
        throw new RequestError(403, "request.origin_rejected", "Browser-origin requests are not accepted.");
    }
    const expected = Deno.env.get("MARKETPLACE_PUBLICATION_SIGNER_SECRET") ?? "";
    const provided = request.headers.get("x-keire-publication-secret") ?? "";
    if (expected.length < 32 || expected.length > 256 || provided.length > 256) {
        throw new RequestError(503, "publication.not_configured", "The publication queue is not configured.");
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
    if (difference !== 0) throw new RequestError(401, "publication.authentication_failed", "Authentication failed.");
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

function decodeBase64(value: string, expectedBytes: number): ArrayBuffer {
    if (!/^[A-Za-z0-9+/]+={0,2}$/.test(value) || value.length > 4096) {
        throw new RequestError(400, "publication.signature_invalid", "The publication signature is malformed.");
    }
    try {
        const decoded = Uint8Array.from(atob(value), (character) => character.charCodeAt(0));
        if (decoded.byteLength !== expectedBytes || btoa(String.fromCharCode(...decoded)) !== value) throw new Error();
        const result = new ArrayBuffer(decoded.byteLength);
        new Uint8Array(result).set(decoded);
        return result;
    } catch {
        throw new RequestError(400, "publication.signature_invalid", "The publication signature is malformed.");
    }
}

async function verifyPublication(
    admin: SupabaseClient,
    signedManifest: string,
): Promise<{ keyId: string; document: Record<string, unknown> }> {
    let envelope: Record<string, unknown>;
    let document: Record<string, unknown>;
    try {
        const parsed: unknown = JSON.parse(signedManifest);
        if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error("envelope");
        envelope = parsed as Record<string, unknown>;
        if (envelope.schemaVersion !== 1 || typeof envelope.document !== "string") throw new Error("schema");
        const parsedDocument: unknown = JSON.parse(envelope.document);
        if (!parsedDocument || typeof parsedDocument !== "object" || Array.isArray(parsedDocument)) throw new Error("document");
        document = parsedDocument as Record<string, unknown>;
    } catch {
        throw new RequestError(400, "publication.manifest_invalid", "The signed publication envelope is malformed.");
    }
    const signature = envelope.signature;
    if (!signature || typeof signature !== "object" || Array.isArray(signature)) {
        throw new RequestError(400, "publication.signature_invalid", "The publication signature is missing.");
    }
    const signatureValue = signature as Record<string, unknown>;
    if (signatureValue.algorithm !== "ed25519" || typeof signatureValue.keyId !== "string" ||
        typeof signatureValue.value !== "string" || document.keyId !== signatureValue.keyId ||
        document.sequence !== signatureValue.sequence || document.expiresAt !== signatureValue.expiresAt) {
        throw new RequestError(400, "publication.signature_invalid", "The publication signature metadata is invalid.");
    }
    const { data: keyData, error } = await admin.from("marketplace_signature_keys")
        .select("key_id,algorithm,public_key_base64,active,valid_from,valid_until")
        .eq("key_id", signatureValue.keyId).eq("active", true).maybeSingle();
    const key = keyData as null | {
        algorithm: string; public_key_base64: string; valid_from: string; valid_until: string | null;
    };
    if (error || !key || key.algorithm !== "ed25519" || Date.parse(key.valid_from) > Date.now() ||
        (key.valid_until && Date.parse(key.valid_until) <= Date.now())) {
        throw new RequestError(409, "publication.key_untrusted", "The Marketplace signing key is not trusted.");
    }
    const verificationKey = await crypto.subtle.importKey(
        "raw", decodeBase64(key.public_key_base64, 32), { name: "Ed25519" }, false, ["verify"]);
    if (!await crypto.subtle.verify(
        "Ed25519", verificationKey, decodeBase64(signatureValue.value, 64),
        new TextEncoder().encode(envelope.document as string))) {
        throw new RequestError(409, "publication.signature_invalid", "The Marketplace signature is invalid.");
    }
    return { keyId: signatureValue.keyId, document };
}

Deno.serve(async (request: Request) => {
    try {
        if (request.method !== "POST") throw new RequestError(405, "request.method_not_allowed", "POST is required.");
        await authenticate(request);
        const url = Deno.env.get("SUPABASE_URL") ?? "";
        const key = serviceKey();
        if (!url || !key) throw new RequestError(503, "publication.not_configured", "The publication queue is not configured.");
        const admin = createClient(url, key, {
            auth: { autoRefreshToken: false, persistSession: false },
            global: { headers: { "X-Client-Info": "keire-publication-queue/0.4.1" } },
        });
        const input = await readJson(request);
        const action = stringField(input, "action", 4, 16);
        const workerId = stringField(input, "workerId", 3, 128);
        if (!/^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(workerId)) {
            throw new RequestError(400, "request.invalid_field", "workerId is invalid.");
        }
        if (action === "lease") {
            const { data, error } = await admin.rpc("service_lease_marketplace_publication", {
                p_worker_id: workerId,
                p_signing_key_id: stringField(input, "signingKeyId", 1, 64),
                p_lease_seconds: integerField(input, "leaseSeconds", 60, 900),
            });
            if (error) throw new RequestError(503, "publication.lease_failed", "A publication lease could not be acquired.");
            const lease = Array.isArray(data) ? data[0] : null;
            return json(200, { data: { lease: lease ?? null } });
        }
        if (action === "renew") {
            const { data, error } = await admin.rpc("service_renew_marketplace_publication_lease", {
                p_job_id: uuidField(input, "jobId"),
                p_worker_id: workerId,
                p_lease_seconds: integerField(input, "leaseSeconds", 60, 900),
            });
            if (error) throw new RequestError(409, "publication.lease_lost", "The publication lease was lost.");
            return json(200, { data: { leaseExpiresAt: data } });
        }
        if (action === "complete") {
            const signedManifest = stringField(input, "signedManifest", 2, 64 * 1024);
            await verifyPublication(admin, signedManifest);
            const { data, error } = await admin.rpc("service_publish_marketplace_version_v2", {
                p_job_id: uuidField(input, "jobId"),
                p_worker_id: workerId,
                p_signed_manifest: signedManifest,
            });
            if (error) throw new RequestError(409, "publication.commit_failed", "The signed publication could not be committed.");
            const result = Array.isArray(data) ? data[0] : data;
            return json(201, { data: result });
        }
        if (action === "fail") {
            if (typeof input.retryable !== "boolean") {
                throw new RequestError(400, "request.invalid_field", "retryable is invalid.");
            }
            const { data, error } = await admin.rpc("service_fail_marketplace_publication", {
                p_job_id: uuidField(input, "jobId"),
                p_worker_id: workerId,
                p_error_code: stringField(input, "errorCode", 3, 128),
                p_retryable: input.retryable,
            });
            if (error) throw new RequestError(409, "publication.failure_rejected", "The publication failure was not accepted.");
            return json(200, { data: { state: data } });
        }
        throw new RequestError(400, "request.action_invalid", "The publication queue action is invalid.");
    } catch (error) {
        if (error instanceof RequestError) return json(error.status, { error: { code: error.code, message: error.message } });
        console.error("publication queue failed", error instanceof Error ? error.message : String(error));
        return json(500, { error: { code: "publication.internal_error", message: "The publication queue failed." } });
    }
});
