import type { APIContext } from "astro";
import { getAssuranceState } from "./auth";

export class MarketplaceApiError extends Error {
    readonly status: number;
    readonly code: string;
    readonly details?: Record<string, unknown>;

    constructor(status: number, code: string, message: string, details?: Record<string, unknown>) {
        super(message);
        this.name = "MarketplaceApiError";
        this.status = status;
        this.code = code;
        this.details = details;
    }
}

export function apiResponse(context: APIContext, value: unknown, status = 200, cacheControl = "no-store"): Response {
    return new Response(JSON.stringify(value), {
        status,
        headers: {
            "content-type": "application/json; charset=utf-8",
            "cache-control": cacheControl,
            "x-correlation-id": context.locals.correlationId,
        },
    });
}

export function apiError(context: APIContext, error: unknown): Response {
    const normalized = error instanceof MarketplaceApiError
        ? error
        : new MarketplaceApiError(500, "marketplace.internal_error", "The marketplace request could not be completed.");
    if (!(error instanceof MarketplaceApiError)) {
        console.error(JSON.stringify({
            level: "error",
            event: "marketplace.request_failed",
            correlationId: context.locals.correlationId,
            path: context.url.pathname,
            error: error instanceof Error ? error.message : String(error),
        }));
    }
    return apiResponse(context, {
        error: {
            code: normalized.code,
            message: normalized.message,
            correlationId: context.locals.correlationId,
            ...(normalized.details ? { details: normalized.details } : {}),
        },
    }, normalized.status);
}

export function requireSupabase(context: APIContext) {
    if (!context.locals.supabase) {
        throw new MarketplaceApiError(503, "marketplace.not_configured", "Marketplace staging is not configured.");
    }
    return context.locals.supabase;
}

export function requireUser(context: APIContext) {
    if (!context.locals.user) {
        throw new MarketplaceApiError(401, "account.authentication_required", "Sign in to continue.");
    }
    return context.locals.user;
}

export async function throwEdgeFunctionError(error: unknown): Promise<never> {
    const context = error && typeof error === "object" && "context" in error
        ? (error as { context?: unknown }).context
        : null;
    if (context instanceof Response) {
        try {
            const payload = await context.json() as { error?: { code?: unknown; message?: unknown } };
            const code = payload.error?.code;
            const message = payload.error?.message;
            if (typeof code === "string" && /^[a-z][a-z0-9_.-]{2,95}$/.test(code) &&
                typeof message === "string" && message.length >= 1 && message.length <= 500) {
                const status = context.status >= 400 && context.status <= 599 ? context.status : 503;
                throw new MarketplaceApiError(status, code, message);
            }
        } catch (parseError) {
            if (parseError instanceof MarketplaceApiError) throw parseError;
        }
    }
    throw new MarketplaceApiError(503, "marketplace.transition_failed",
        "The marketplace operation could not be completed.");
}

export async function requireAal2(context: APIContext): Promise<void> {
    const supabase = requireSupabase(context);
    requireUser(context);
    const state = await getAssuranceState(supabase);
    if (state.currentLevel !== "aal2") {
        throw new MarketplaceApiError(403, "account.mfa_required", "Verify a second factor to continue.");
    }
}

export async function parseJsonObject(context: APIContext, maximumBytes = 64 * 1024): Promise<Record<string, unknown>> {
    const declaredLength = Number.parseInt(context.request.headers.get("content-length") ?? "0", 10);
    if (Number.isFinite(declaredLength) && declaredLength > maximumBytes) {
        throw new MarketplaceApiError(413, "request.too_large", "The request body exceeds its size limit.");
    }
    const contentType = context.request.headers.get("content-type")?.split(";", 1)[0].trim().toLowerCase();
    if (contentType !== "application/json") {
        throw new MarketplaceApiError(415, "request.unsupported_media_type", "The request body must use application/json.");
    }
    if (!context.request.body) {
        throw new MarketplaceApiError(400, "request.invalid_json", "The request body must be valid JSON.");
    }

    const reader = context.request.body.getReader();
    const chunks: Uint8Array[] = [];
    let byteLength = 0;
    try {
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            byteLength += value.byteLength;
            if (byteLength > maximumBytes) {
                await reader.cancel("request body exceeded its limit");
                throw new MarketplaceApiError(413, "request.too_large", "The request body exceeds its size limit.");
            }
            chunks.push(value);
        }
    } finally {
        reader.releaseLock();
    }

    const bytes = new Uint8Array(byteLength);
    let offset = 0;
    for (const chunk of chunks) {
        bytes.set(chunk, offset);
        offset += chunk.byteLength;
    }
    let value: unknown;
    try {
        value = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    } catch {
        throw new MarketplaceApiError(400, "request.invalid_json", "The request body must be valid JSON.");
    }
    if (!value || typeof value !== "object" || Array.isArray(value)) {
        throw new MarketplaceApiError(400, "request.invalid_shape", "The request body must be a JSON object.");
    }
    return value as Record<string, unknown>;
}

export function boundedString(value: unknown, name: string, minimum: number, maximum: number): string {
    if (typeof value !== "string" || value.length < minimum || value.length > maximum) {
        throw new MarketplaceApiError(400, "request.invalid_field", `${name} must contain ${minimum}-${maximum} characters.`,
            { field: name });
    }
    return value;
}

export interface CatalogCursor {
    featured: boolean;
    publishedAt: string;
    id: string;
}

export interface LibraryCursor {
    grantedAt: string;
    id: string;
}

const cursorUuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;

function cursorTimestamp(value: unknown): string {
    if (typeof value !== "string" || value.length > 64) throw new Error("invalid timestamp");
    const parsed = new Date(value);
    if (!Number.isFinite(parsed.valueOf())) throw new Error("invalid timestamp");
    return parsed.toISOString();
}

function cursorId(value: unknown): string {
    if (typeof value !== "string" || !cursorUuidPattern.test(value)) throw new Error("invalid identity");
    return value.toLowerCase();
}

function decodeCursor(value: string | null): Record<string, unknown> | null {
    if (!value) return null;
    try {
        if (value.length > 512) throw new Error("oversized cursor");
        const decoded: unknown = JSON.parse(Buffer.from(value, "base64url").toString("utf8"));
        if (!decoded || typeof decoded !== "object" || Array.isArray(decoded)) throw new Error("invalid cursor");
        return decoded as Record<string, unknown>;
    } catch {
        throw new MarketplaceApiError(400, "marketplace.invalid_cursor", "The pagination cursor is invalid.");
    }
}

export function decodeCatalogCursor(value: string | null): CatalogCursor | null {
    const decoded = decodeCursor(value);
    if (!decoded) return null;
    try {
        if (decoded.kind !== "catalog" || typeof decoded.featured !== "boolean") throw new Error("wrong cursor kind");
        return {
            featured: decoded.featured,
            publishedAt: cursorTimestamp(decoded.publishedAt),
            id: cursorId(decoded.id),
        };
    } catch {
        throw new MarketplaceApiError(400, "marketplace.invalid_cursor", "The pagination cursor is invalid.");
    }
}

export function encodeCatalogCursor(cursor: CatalogCursor): string {
    return Buffer.from(JSON.stringify({ kind: "catalog", ...cursor }), "utf8").toString("base64url");
}

export function decodeLibraryCursor(value: string | null): LibraryCursor | null {
    const decoded = decodeCursor(value);
    if (!decoded) return null;
    try {
        if (decoded.kind !== "library") throw new Error("wrong cursor kind");
        return { grantedAt: cursorTimestamp(decoded.grantedAt), id: cursorId(decoded.id) };
    } catch {
        throw new MarketplaceApiError(400, "marketplace.invalid_cursor", "The pagination cursor is invalid.");
    }
}

export function encodeLibraryCursor(cursor: LibraryCursor): string {
    return Buffer.from(JSON.stringify({ kind: "library", ...cursor }), "utf8").toString("base64url");
}
