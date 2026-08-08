export const maximumRequestBytes = 16 * 1024;

export class ContactRequestError extends Error {
    /**
     * @param {number} status
     * @param {string} message
     */
    constructor(status, message) {
        super(message);
        this.name = "ContactRequestError";
        this.status = status;
    }
}

/**
 * Reads and parses a request without trusting Content-Length as the allocation boundary.
 *
 * @param {Request} request
 * @returns {Promise<Record<string, unknown>>}
 */
export async function readBoundedJson(request) {
    const declaredLength = request.headers.get("content-length");
    if (declaredLength !== null) {
        if (!/^\d+$/u.test(declaredLength)) {
            throw new ContactRequestError(400, "The request length is invalid.");
        }
        if (Number(declaredLength) > maximumRequestBytes) {
            throw new ContactRequestError(413, "The request is too large.");
        }
    }

    if (request.body === null) {
        throw new ContactRequestError(400, "The request could not be read.");
    }

    const reader = request.body.getReader();
    /** @type {Uint8Array[]} */
    const chunks = [];
    let received = 0;
    try {
        for (;;) {
            const { done, value } = await reader.read();
            if (done) {
                break;
            }
            received += value.byteLength;
            if (received > maximumRequestBytes) {
                await reader.cancel("request body limit exceeded");
                throw new ContactRequestError(413, "The request is too large.");
            }
            chunks.push(value);
        }
    } finally {
        reader.releaseLock();
    }

    const bytes = new Uint8Array(received);
    let offset = 0;
    for (const chunk of chunks) {
        bytes.set(chunk, offset);
        offset += chunk.byteLength;
    }

    try {
        const parsed = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
        if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
            throw new TypeError("request JSON must be an object");
        }
        return parsed;
    } catch {
        throw new ContactRequestError(400, "The request could not be read.");
    }
}

/**
 * Supabase's edge is the only trusted writer of this Cloudflare-derived header. Caller-controlled forwarding headers
 * are deliberately ignored so they cannot partition the rate-limit key space.
 *
 * @param {Request} request
 * @returns {string}
 */
export function requestAddress(request) {
    const address = request.headers.get("cf-connecting-ip")?.trim() ?? "";
    return (address || "unknown").slice(0, 128);
}
