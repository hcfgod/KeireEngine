import { isIP } from "node:net";

function singleHeader(request, name) {
    let occurrences = 0;
    for (let index = 0; index < request.rawHeaders.length; index += 2) {
        if (request.rawHeaders[index].toLowerCase() === name) {
            occurrences++;
        }
    }
    const value = request.headers[name];
    if (occurrences !== 1 || typeof value !== "string") {
        throw new Error("Invalid reverse-proxy request metadata.");
    }
    return value;
}

function canonicalIp(value) {
    if (isIP(value) === 4) {
        return value;
    }
    if (isIP(value) === 6 && !value.includes("%")) {
        const canonical = new URL(`http://[${value}]/`).hostname.slice(1, -1);
        // Normalize IPv4-mapped peers and clients so alternate spelling cannot create extra rate-limit buckets.
        const mapped = /^::ffff:([0-9a-f]{1,4}):([0-9a-f]{1,4})$/.exec(canonical);
        if (mapped) {
            const high = parseInt(mapped[1], 16);
            const low = parseInt(mapped[2], 16);
            return `${high >> 8}.${high & 255}.${low >> 8}.${low & 255}`;
        }
        return canonical;
    }
    throw new Error("Invalid reverse-proxy address.");
}

/** Caddy must overwrite X-Keire-Client-IP with its actual remote peer; forwarded chains are never trusted. */
export function createProductionProxyPolicy(siteOrigin) {
    const configured = new URL(siteOrigin);
    if (configured.protocol !== "https:" || configured.origin !== siteOrigin) {
        throw new Error("Production proxy requires a canonical HTTPS origin.");
    }
    return Object.freeze({
        authorize(request) {
            const peer = canonicalIp(request.socket.remoteAddress ?? "");
            if (peer !== "127.0.0.1" && peer !== "::1") {
                throw new Error("Only the local reverse proxy may contact this listener.");
            }
            if (typeof request.url !== "string" || !request.url.startsWith("/") || request.url.startsWith("//") ||
                /[\\\u0000-\u0020\u007f]/.test(request.url)) {
                throw new Error("Invalid request target.");
            }
            if (singleHeader(request, "host") !== configured.host ||
                singleHeader(request, "x-forwarded-host") !== configured.host ||
                singleHeader(request, "x-forwarded-proto") !== "https") {
                throw new Error("Reverse proxy host or protocol did not match deployment.");
            }
            const client = canonicalIp(singleHeader(request, "x-keire-client-ip"));
            // Downstream integrations receive only normalized, verified forwarding metadata.
            delete request.headers.forwarded;
            delete request.headers["x-forwarded-for"];
            delete request.headers["x-real-ip"];
            delete request.headers["x-keire-client-ip"];
            request.headers["x-forwarded-proto"] = "https";
            request.headers["x-forwarded-host"] = configured.host;
            request.headers.host = configured.host;
            return `ip:${client}`;
        },
        healthRequest(request) {
            const peer = canonicalIp(request.socket.remoteAddress ?? "");
            return (peer === "127.0.0.1" || peer === "::1") && ["GET", "HEAD"].includes(request.method) &&
                ["/health", "/health/"].includes(request.url);
        },
    });
}
