import { createPrivateKey, generateKeyPairSync } from "node:crypto";
import { createNativeIdentitySigner } from "./platform-native-identity.mjs";
import { createNativeOAuthPolicy } from "./platform-native-oauth-policy.mjs";

/** Setup only: return to a private pipe for OS-protected storage, never a log or command argument. */
export function generateNativeSettings(siteOrigin, clientId) {
    createNativeOAuthPolicy({ siteOrigin, clientId });
    const { privateKey } = generateKeyPairSync("ed25519");
    const signer = createNativeIdentitySigner({ siteOrigin, clientId, privateKey });
    try {
        return {
            schemaVersion: 1,
            siteOrigin,
            clientId,
            keyId: signer.jwks.keys[0].kid,
            privateKeyPkcs8: privateKey.export({ format: "der", type: "pkcs8" }).toString("base64"),
        };
    } finally {
        signer.close();
    }
}

/** Parse only decrypted host-owned settings; reject keys bound to another host or registration. */
export function loadNativeSettings(settings, expectedOrigin, expectedClientId) {
    let bytes;
    try {
        if (!settings || Object.keys(settings).sort().join(",") !== "clientId,keyId,privateKeyPkcs8,schemaVersion,siteOrigin"
            || settings.schemaVersion !== 1 || settings.siteOrigin !== expectedOrigin
            || settings.clientId !== expectedClientId || typeof settings.privateKeyPkcs8 !== "string"
            || settings.privateKeyPkcs8.length > 1024) {
            throw new Error();
        }
        createNativeOAuthPolicy({ siteOrigin: expectedOrigin, clientId: expectedClientId });
        bytes = Buffer.from(settings.privateKeyPkcs8, "base64");
        if (bytes.toString("base64") !== settings.privateKeyPkcs8) {
            throw new Error();
        }
        const privateKey = createPrivateKey({ key: bytes, format: "der", type: "pkcs8" });
        const signer = createNativeIdentitySigner({ siteOrigin: expectedOrigin, clientId: expectedClientId, privateKey });
        try {
            if (signer.jwks.keys[0].kid !== settings.keyId) {
                throw new Error();
            }
        } finally {
            signer.close();
        }
        return Object.freeze({ siteOrigin: expectedOrigin, clientId: expectedClientId, privateKey });
    } catch {
        // Crypto/parser diagnostics can contain input; expose only a fixed configuration error.
        throw new Error("Protected native authentication settings are invalid or do not match this host.");
    } finally {
        bytes?.fill(0);
    }
}
