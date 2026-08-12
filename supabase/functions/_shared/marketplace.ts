import { createClient, type SupabaseClient, type User } from "npm:@supabase/supabase-js@2.112.3";

const allowedOrigins = new Set(["https://keireengine.duckdns.org"]);
const maximumRequestBytes = 64 * 1024;

export class RequestError extends Error {
    constructor(readonly status: number, readonly code: string, message: string) {
        super(message);
        this.name = "RequestError";
    }
}

function responseHeaders(origin: string): HeadersInit {
    return {
        ...(origin ? { "Access-Control-Allow-Origin": origin } : {}),
        "Access-Control-Allow-Headers": "authorization, content-type, x-client-info, apikey",
        "Access-Control-Allow-Methods": "POST, OPTIONS",
        "Access-Control-Max-Age": "86400",
        "Cache-Control": "no-store",
        "Content-Type": "application/json; charset=utf-8",
        "Vary": "Origin",
    };
}

export function json(origin: string, status: number, body: Record<string, unknown>): Response {
    return new Response(JSON.stringify(body), { status, headers: responseHeaders(origin) });
}

export function validateOrigin(request: Request): string {
    const origin = request.headers.get("origin") ?? "";
    if (origin && !allowedOrigins.has(origin)) {
        throw new RequestError(403, "request.origin_rejected", "The request origin is not allowed.");
    }
    return origin;
}

export function preflight(origin: string): Response {
    return new Response(null, { status: 204, headers: responseHeaders(origin) });
}

export async function readJson(request: Request): Promise<Record<string, unknown>> {
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
    const bytes = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) {
        bytes.set(chunk, offset);
        offset += chunk.byteLength;
    }
    try {
        const parsed: unknown = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
        if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error("not an object");
        return parsed as Record<string, unknown>;
    } catch (error) {
        if (error instanceof RequestError) throw error;
        throw new RequestError(400, "request.invalid_json", "The request body must be a JSON object.");
    }
}

function secretKey(): string | null {
    try {
        const keys = JSON.parse(Deno.env.get("SUPABASE_SECRET_KEYS") ?? "{}") as Record<string, unknown>;
        if (typeof keys.default === "string" && keys.default) return keys.default;
    } catch {
        // The standard service-role variable remains the supported fallback.
    }
    return Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ?? null;
}

function tokenClaims(token: string): Record<string, unknown> | null {
    try {
        const parts = token.split(".");
        if (parts.length !== 3 || parts[1].length > 16 * 1024) return null;
        const normalized = parts[1].replaceAll("-", "+").replaceAll("_", "/");
        const padding = "=".repeat((4 - (normalized.length % 4)) % 4);
        const payload: unknown = JSON.parse(atob(normalized + padding));
        return payload && typeof payload === "object" && !Array.isArray(payload)
            ? payload as Record<string, unknown>
            : null;
    } catch {
        return null;
    }
}

export interface Caller {
    user: User;
    sessionId: string | null;
    assuranceLevel: string | null;
    admin: SupabaseClient;
}

export async function authenticate(request: Request): Promise<Caller> {
    const token = request.headers.get("authorization")?.match(/^Bearer ([\x21-\x7e]{16,16384})$/i)?.[1];
    const url = Deno.env.get("SUPABASE_URL") ?? "";
    const publishableKey = Deno.env.get("SUPABASE_ANON_KEY") ?? "";
    const adminKey = secretKey();
    if (!token) throw new RequestError(401, "account.authentication_required", "Authentication is required.");
    if (!url || !publishableKey || !adminKey) {
        throw new RequestError(503, "marketplace.not_configured", "Marketplace services are not configured.");
    }
    const userClient = createClient(url, publishableKey, {
        auth: { autoRefreshToken: false, persistSession: false },
        global: { headers: { Authorization: `Bearer ${token}` } },
    });
    const { data, error } = await userClient.auth.getUser(token);
    if (error || !data.user) throw new RequestError(401, "account.authentication_required", "Authentication is invalid or expired.");
    const claims = tokenClaims(token);
    return {
        user: data.user,
        sessionId: typeof claims?.session_id === "string" ? claims.session_id : null,
        assuranceLevel: typeof claims?.aal === "string" ? claims.aal : null,
        admin: createClient(url, adminKey, {
            auth: { autoRefreshToken: false, persistSession: false },
            global: { headers: { "X-Client-Info": "keire-marketplace-edge/0.3.1" } },
        }),
    };
}

export function stringField(input: Record<string, unknown>, name: string, minimum: number, maximum: number): string {
    const value = input[name];
    if (typeof value !== "string" || value.length < minimum || value.length > maximum) {
        throw new RequestError(400, "request.invalid_field", `${name} must contain ${minimum}-${maximum} characters.`);
    }
    return value;
}

export function optionalUuid(input: Record<string, unknown>, name: string): string | null {
    const value = input[name];
    if (value == null) return null;
    if (typeof value !== "string" || !/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value)) {
        throw new RequestError(400, "request.invalid_field", `${name} must be a UUID.`);
    }
    return value;
}

export function requiredUuid(input: Record<string, unknown>, name: string): string {
    const value = optionalUuid(input, name);
    if (!value) throw new RequestError(400, "request.invalid_field", `${name} must be a UUID.`);
    return value;
}

export function databaseFailure(error: { message: string; code?: string }): RequestError {
    const mappings = new Map<string, readonly [number, string, string]>([
        ["marketplace_disabled", [503, "marketplace.disabled", "The marketplace is not available yet."]],
        ["hub_oauth_disabled", [503, "account.hub_oauth_disabled", "Hub account sign-in is not available yet."]],
        ["publisher_portal_disabled", [503, "publisher.disabled", "Publisher tools are not available yet."]],
        ["publisher_application_not_editable", [409, "publisher.application_not_editable", "This publisher application can no longer be edited."]],
        ["publisher_application_decision_invalid", [400, "staff.application_decision_invalid", "The publisher decision is incomplete or invalid."]],
        ["publisher_application_not_reviewable", [409, "staff.application_not_reviewable", "This publisher application is no longer awaiting review."]],
        ["publisher_product_not_editable", [403, "publisher.product_not_editable", "This product can no longer be edited."]],
        ["publisher_version_not_editable", [409, "publisher.version_not_editable", "This package version can no longer be edited."]],
        ["publisher_version_not_submittable", [409, "publisher.version_not_submittable", "Only a validated package version can be submitted for staff review."]],
        ["publisher_validation_required", [409, "publisher.validation_required", "A passing validation report is required before submission."]],
        ["publisher_submission_already_active", [409, "publisher.submission_active", "This package version already has an active staff submission."]],
        ["publisher_upload_invalid", [400, "publisher.upload_invalid", "The package upload request is invalid."]],
        ["publisher_upload_already_active", [409, "publisher.upload_already_active", "This version already has an active package upload."]],
        ["publisher_upload_not_completable", [409, "publisher.upload_not_completable", "The package upload cannot be completed in its current state."]],
        ["publisher_upload_not_cancellable", [409, "publisher.upload_not_cancellable", "The package upload can no longer be cancelled."]],
        ["publisher_upload_size_mismatch", [409, "publisher.upload_size_mismatch", "The uploaded package size does not match the selected file."]],
        ["asset_packages_disabled", [503, "marketplace.asset_packages_disabled", "Asset package delivery is not available yet."]],
        ["organization_authorization_required", [403, "marketplace.organization_forbidden", "Your account cannot perform this action for the organization."]],
        ["license_revision_changed", [409, "marketplace.license_changed", "The package license changed. Review it before continuing."]],
        ["product_not_found", [404, "marketplace.product_not_found", "The marketplace product was not found."]],
        ["device_session_revoked", [401, "account.session_revoked", "This device session is no longer active."]],
        ["device_session_invalid", [401, "account.session_revoked", "This device session is no longer active."]],
        ["entitlement_required", [403, "marketplace.entitlement_required", "Add this product to your library before downloading it."]],
        ["version_unavailable", [404, "marketplace.version_unavailable", "This package version is not available."]],
        ["marketplace_rate_limited", [429, "marketplace.rate_limited", "Too many marketplace changes were requested. Wait a few minutes, then try again."]],
        ["staff_authorization_required", [403, "staff.authorization_required", "An active staff role is required."]],
        ["staff_moderator_required", [403, "staff.moderator_required", "An active moderator role is required."]],
        ["staff_administrator_required", [403, "staff.administrator_required", "An active administrator role is required."]],
        ["staff_assignment_invalid", [400, "staff.assignment_invalid", "The staff assignment is invalid."]],
        ["staff_last_administrator_required", [409, "staff.last_administrator_required", "At least one active administrator must remain."]],
        ["marketplace_submission_decision_invalid", [400, "staff.submission_decision_invalid", "The package decision is incomplete or invalid."]],
        ["marketplace_submission_not_reviewable", [409, "staff.submission_not_reviewable", "This package submission is no longer awaiting review."]],
        ["marketplace_submission_validation_failed", [409, "staff.submission_validation_required", "This package does not have a passing validation report."]],
        ["marketplace_report_decision_invalid", [400, "staff.report_decision_invalid", "The report decision is incomplete or invalid."]],
        ["marketplace_report_not_reviewable", [409, "staff.report_not_reviewable", "This report is no longer awaiting review."]],
        ["platform_feature_flag_change_invalid", [400, "staff.feature_flag_invalid", "The feature-gate change is not allowed."]],
    ]);
    const mapped = mappings.get(error.message);
    if (!mapped && error.code === "23505") {
        return new RequestError(409, "marketplace.conflict", "That identifier is already in use. Choose another one.");
    }
    return mapped
        ? new RequestError(mapped[0], mapped[1], mapped[2])
        : new RequestError(503, "marketplace.transition_failed", "The marketplace operation could not be completed.");
}

export function handleError(origin: string, error: unknown): Response {
    if (error instanceof RequestError) return json(origin, error.status, { error: { code: error.code, message: error.message } });
    console.error("marketplace edge transition failed", error instanceof Error ? error.message : String(error));
    return json(origin, 500, { error: { code: "marketplace.internal_error", message: "The marketplace operation failed." } });
}
