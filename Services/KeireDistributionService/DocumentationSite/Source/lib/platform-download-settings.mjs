import { randomBytes } from "node:crypto";

function validateOrigin(origin) {
    const address = new URL(origin);
    if (address.protocol !== "https:" || address.origin !== origin) {
        throw new Error("Invalid download origin.");
    }
}

/** Setup only. Send this result directly to OS-protected storage, never to logs. */
export function generateDownloadSettings(siteOrigin) {
    validateOrigin(siteOrigin);
    const key = randomBytes(32);
    try {
        return { schemaVersion: 1, siteOrigin, downloadKey: key.toString("base64") };
    } finally {
        key.fill(0);
    }
}

/** The caller owns the returned key and must clear it after the service copies it. */
export function loadDownloadSettings(settings, expectedOrigin) {
    let key;
    try {
        validateOrigin(expectedOrigin);
        if (!settings || Object.keys(settings).sort().join(",") !== "downloadKey,schemaVersion,siteOrigin"
            || settings.schemaVersion !== 1 || settings.siteOrigin !== expectedOrigin
            || typeof settings.downloadKey !== "string" || settings.downloadKey.length !== 44) {
            throw new Error();
        }
        key = Buffer.from(settings.downloadKey, "base64");
        if (key.length !== 32 || key.toString("base64") !== settings.downloadKey) {
            throw new Error();
        }
        return key;
    } catch {
        key?.fill(0);
        throw new Error("Protected download settings are invalid or do not match this host.");
    }
}
