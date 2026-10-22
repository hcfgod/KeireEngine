import { expect, it } from "vitest";
import { generateDownloadSettings, loadDownloadSettings } from "./platform-download-settings.mjs";

const origin = "https://platform.invalid";

it("generates independent keys and reloads the protected payload without replacing it", () => {
    const settings = generateDownloadSettings(origin);
    const first = loadDownloadSettings(settings, origin);
    const second = loadDownloadSettings(settings, origin);
    expect(first).toEqual(second);
    expect(first.length).toBe(32);
    expect(generateDownloadSettings(origin).downloadKey).not.toBe(settings.downloadKey);
    first.fill(0);
    expect(second.toString("base64")).toBe(settings.downloadKey);
    second.fill(0);
});

it.each([
    (settings) => ({ ...settings, schemaVersion: 2 }),
    (settings) => ({ ...settings, siteOrigin: "https://other.invalid" }),
    (settings) => ({ ...settings, downloadKey: "secret-invalid-value" }),
    (settings) => ({ ...settings, privateKey: "unexpected" }),
    (settings) => ({ ...settings, downloadKey: settings.downloadKey.slice(0, -1) + "A" }),
])("rejects mismatched or malformed settings without exposing input", (change) => {
    expect(() => loadDownloadSettings(change(generateDownloadSettings(origin)), origin))
        .toThrow("Protected download settings are invalid or do not match this host.");
});

it.each(["http://platform.invalid", "https://platform.invalid/", "https://user:pass@platform.invalid"])(
    "rejects noncanonical origin %s", (address) => {
        expect(() => generateDownloadSettings(address)).toThrow();
    });
