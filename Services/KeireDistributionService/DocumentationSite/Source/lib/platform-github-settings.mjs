import { githubConfiguration } from "./platform-github-provider.mjs";

/** Returns an owned buffer; bootstrap must clear it after the service has made its copy. */
export function loadGitHubSettings(settings, expectedOrigin) {
    let secret;
    try {
        if (!settings || Object.keys(settings).sort().join(",") !== "clientId,clientSecret,schemaVersion,siteOrigin" ||
            settings.schemaVersion !== 1 || settings.siteOrigin !== expectedOrigin ||
            typeof settings.clientSecret !== "string" || settings.clientSecret.length > 684) {
            throw new Error();
        }
        secret = Buffer.from(settings.clientSecret, "base64");
        if (secret.toString("base64") !== settings.clientSecret || !/^[A-Za-z0-9_-]{20,512}$/.test(secret.toString("utf8"))) {
            throw new Error();
        }
        const configuration = githubConfiguration({ ...settings, clientSecret: secret });
        return { siteOrigin: configuration.siteOrigin, clientId: configuration.clientId, clientSecret: secret };
    } catch {
        secret?.fill(0);
        throw new Error("Protected GitHub settings are invalid or do not match this host.");
    }
}
