import { generateKeyPairSync } from "node:crypto";
import { describe, expect, it } from "vitest";
import { generateNativeSettings, loadNativeSettings } from "./platform-native-settings.mjs";
import { createNativeIdentitySigner } from "./platform-native-identity.mjs";

const origin = "https://platform.invalid";
const clientId = "keire-hub-desktop";

describe("protected native key configuration", () => {
    it("round trips a stable identity without generating a replacement during loading", () => {
        const settings = generateNativeSettings(origin, clientId);
        for (let attempt = 0; attempt < 2; attempt++) {
            const loaded = loadNativeSettings(JSON.parse(JSON.stringify(settings)), origin, clientId);
            const signer = createNativeIdentitySigner(loaded);
            expect(signer.jwks.keys[0].kid).toBe(settings.keyId);
            expect(signer.jwks.keys[0]).not.toHaveProperty("d");
            signer.close();
        }
    });

    it.each([
        { schemaVersion: 2 }, { siteOrigin: "https://different.invalid" }, { clientId: "another-client" },
        { keyId: "wrong" }, { privateKeyPkcs8: "private-invalid-value" }, { unexpected: true },
    ])("rejects mismatched or malformed settings with a safe message: %j", (change) => {
        const settings = { ...generateNativeSettings(origin, clientId), ...change };
        expect(() => loadNativeSettings(settings, origin, clientId))
            .toThrow("Protected native authentication settings are invalid or do not match this host.");
    });

    it("rejects a valid private key of the wrong algorithm", () => {
        const settings = generateNativeSettings(origin, clientId);
        settings.privateKeyPkcs8 = generateKeyPairSync("ec", { namedCurve: "prime256v1" }).privateKey
            .export({ format: "der", type: "pkcs8" }).toString("base64");
        expect(() => loadNativeSettings(settings, origin, clientId)).toThrow("settings are invalid");
    });

    it("rejects noncanonical base64 instead of accepting ignored characters", () => {
        const settings = generateNativeSettings(origin, clientId);
        settings.privateKeyPkcs8 += "\n";
        expect(() => loadNativeSettings(settings, origin, clientId)).toThrow("settings are invalid");
    });
});
