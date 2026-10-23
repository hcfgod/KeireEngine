import { describe, expect, it } from "vitest";
import { loadGitHubSettings } from "./platform-github-settings.mjs";

const input = () => ({ schemaVersion: 1, siteOrigin: "https://platform.invalid", clientId: "Iv1.clientfixture",
    clientSecret: Buffer.from("synthetic_fixture_" + "a".repeat(30)).toString("base64") });

describe("protected GitHub settings", () => {
    it("returns a caller-owned secret bound to the host", () => {
        const settings = input();
        const loaded = loadGitHubSettings(settings, settings.siteOrigin);
        expect(loaded.clientId).toBe(settings.clientId);
        expect(loaded.clientSecret.toString("base64")).toBe(settings.clientSecret);
        loaded.clientSecret.fill(0);
        expect(loadGitHubSettings(settings, settings.siteOrigin).clientSecret.every((byte) => byte === 0)).toBe(false);
    });

    it.each(["origin", "schema", "extra", "base64", "short", "client"])("rejects invalid %s without revealing the secret", (kind) => {
        const settings = input();
        if (kind === "origin") {
            settings.siteOrigin = "https://other.invalid";
        }
        if (kind === "schema") {
            settings.schemaVersion = 2;
        }
        if (kind === "extra") {
            settings.unexpected = true;
        }
        if (kind === "base64") {
            settings.clientSecret += "!";
        }
        if (kind === "short") {
            settings.clientSecret = Buffer.from("short").toString("base64");
        }
        if (kind === "client") {
            settings.clientId = "invalid client";
        }
        expect(() => loadGitHubSettings(settings, "https://platform.invalid"))
            .toThrow("Protected GitHub settings are invalid or do not match this host.");
    });
});
