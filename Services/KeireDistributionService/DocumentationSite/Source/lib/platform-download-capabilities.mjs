import { createHmac, timingSafeEqual } from "node:crypto";

const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const prefix = "/marketplace/v1/downloads/";
const lifetimeMs = 600000;

/** The host owns a dedicated protected key. Query strings must be redacted from logs. */
export function createDownloadCapabilities({ siteOrigin, key, clock = Date.now }) {
    const origin = new URL(siteOrigin);
    if (origin.protocol !== "https:" || origin.origin !== siteOrigin || !Buffer.isBuffer(key) || key.length !== 32) {
        throw new Error("Download links require a canonical HTTPS origin and a dedicated 32-byte key.");
    }
    let secret = Buffer.from(key);
    const digest = (id, expiry) => createHmac("sha256", secret)
        .update(`Keire.NativeDownload.v1\n${siteOrigin}\n${id}\n${expiry}`).digest();
    const validExpiry = (expiry) => {
        const now = clock();
        return Number.isSafeInteger(now) && Number.isSafeInteger(expiry) && expiry > now && expiry <= now + lifetimeMs;
    };
    return Object.freeze({
        issue(grantId, expiresAt) {
            // Never lengthen the database grant. Cap the link using this process's clock
            // so a slightly ahead database clock does not reject a freshly issued grant.
            const expiry = Math.min(Date.parse(expiresAt), clock() + lifetimeMs);
            if (!secret || !uuid.test(grantId) || !validExpiry(expiry)) {
                throw new Error("The download grant is invalid or expired.");
            }
            return `${siteOrigin}${prefix}${grantId}?expires=${expiry}&signature=${digest(grantId, expiry).toString("base64url")}`;
        },
        verify(address) {
            if (!secret) {
                return null;
            }
            try {
                const url = new URL(address);
                const id = url.pathname.slice(prefix.length);
                const expiryText = url.searchParams.get("expires");
                const signature = url.searchParams.get("signature");
                const expiry = Number(expiryText);
                if (url.origin !== siteOrigin || url.username || url.password || url.hash || !url.pathname.startsWith(prefix)
                    || !uuid.test(id) || url.searchParams.size !== 2 || url.searchParams.getAll("expires").length !== 1
                    || url.searchParams.getAll("signature").length !== 1 || String(expiry) !== expiryText
                    || !validExpiry(expiry) || !/^[A-Za-z0-9_-]{43}$/.test(signature ?? "")) {
                    return null;
                }
                const supplied = Buffer.from(signature, "base64url");
                if (supplied.length !== 32 || supplied.toString("base64url") !== signature
                    || !timingSafeEqual(supplied, digest(id, expiry))) {
                    return null;
                }
                return { grantId: id, expiresMs: expiry };
            } catch {
                return null;
            }
        },
        close() {
            secret?.fill(0);
            secret = null;
        },
    });
}
