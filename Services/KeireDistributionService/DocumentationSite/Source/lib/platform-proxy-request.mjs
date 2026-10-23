/** The origin must come from the production listener's verified locals, never from request headers. */
export function canonicalPlatformRequest(request, verifiedOrigin) {
    if (verifiedOrigin === undefined) {
        return request;
    }
    const origin = new URL(verifiedOrigin);
    const incoming = new URL(request.url);
    if (origin.protocol !== "https:" || origin.origin !== verifiedOrigin || incoming.host !== origin.host) {
        throw new Error("Verified platform request origin mismatch.");
    }
    if (incoming.origin === origin.origin) {
        return request;
    }
    if (incoming.protocol !== "http:") {
        throw new Error("Unsupported platform transport.");
    }
    incoming.protocol = "https:";
    return new Request(incoming, request);
}
