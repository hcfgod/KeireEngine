//#region Source/lib/api.ts
var MarketplaceApiError = class extends Error {
	status;
	code;
	details;
	constructor(status, code, message, details) {
		super(message);
		this.name = "MarketplaceApiError";
		this.status = status;
		this.code = code;
		this.details = details;
	}
};
function apiResponse(context, value, status = 200, cacheControl = "no-store") {
	return new Response(JSON.stringify(value), {
		status,
		headers: {
			"content-type": "application/json; charset=utf-8",
			"cache-control": cacheControl,
			"x-correlation-id": context.locals.correlationId
		}
	});
}
function apiError(context, error) {
	const normalized = error instanceof MarketplaceApiError ? error : new MarketplaceApiError(500, "marketplace.internal_error", "The marketplace request could not be completed.");
	if (!(error instanceof MarketplaceApiError)) console.error(JSON.stringify({
		level: "error",
		event: "marketplace.request_failed",
		correlationId: context.locals.correlationId,
		path: context.url.pathname,
		error: error instanceof Error ? error.message : String(error)
	}));
	return apiResponse(context, { error: {
		code: normalized.code,
		message: normalized.message,
		correlationId: context.locals.correlationId,
		...normalized.details ? { details: normalized.details } : {}
	} }, normalized.status);
}
function requireSupabase(context) {
	if (!context.locals.supabase) throw new MarketplaceApiError(503, "marketplace.not_configured", "Marketplace staging is not configured.");
	return context.locals.supabase;
}
function requireUser(context) {
	if (!context.locals.user) throw new MarketplaceApiError(401, "account.authentication_required", "Sign in to continue.");
	return context.locals.user;
}
async function parseJsonObject(context, maximumBytes = 65536) {
	const declaredLength = Number.parseInt(context.request.headers.get("content-length") ?? "0", 10);
	if (Number.isFinite(declaredLength) && declaredLength > maximumBytes) throw new MarketplaceApiError(413, "request.too_large", "The request body exceeds its size limit.");
	if (context.request.headers.get("content-type")?.split(";", 1)[0].trim().toLowerCase() !== "application/json") throw new MarketplaceApiError(415, "request.unsupported_media_type", "The request body must use application/json.");
	if (!context.request.body) throw new MarketplaceApiError(400, "request.invalid_json", "The request body must be valid JSON.");
	const reader = context.request.body.getReader();
	const chunks = [];
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
	let value;
	try {
		value = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
	} catch {
		throw new MarketplaceApiError(400, "request.invalid_json", "The request body must be valid JSON.");
	}
	if (!value || typeof value !== "object" || Array.isArray(value)) throw new MarketplaceApiError(400, "request.invalid_shape", "The request body must be a JSON object.");
	return value;
}
function boundedString(value, name, minimum, maximum) {
	if (typeof value !== "string" || value.length < minimum || value.length > maximum) throw new MarketplaceApiError(400, "request.invalid_field", `${name} must contain ${minimum}-${maximum} characters.`, { field: name });
	return value;
}
function decodeCursor(value) {
	if (!value) return 0;
	try {
		const decoded = JSON.parse(Buffer.from(value, "base64url").toString("utf8"));
		if (!Number.isSafeInteger(decoded.offset) || decoded.offset < 0 || decoded.offset > 1e5) throw new Error("invalid offset");
		return decoded.offset;
	} catch {
		throw new MarketplaceApiError(400, "marketplace.invalid_cursor", "The pagination cursor is invalid.");
	}
}
function encodeCursor(offset) {
	return Buffer.from(JSON.stringify({ offset }), "utf8").toString("base64url");
}
//#endregion
export { decodeCursor as a, requireSupabase as c, boundedString as i, requireUser as l, apiError as n, encodeCursor as o, apiResponse as r, parseJsonObject as s, MarketplaceApiError as t };
