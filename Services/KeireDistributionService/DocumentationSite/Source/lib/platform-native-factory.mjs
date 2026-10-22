import { loadWindowsNativeSecrets } from "./platform-native-secrets.mjs";
import { createLocalNativeService } from "./platform-native-service.mjs";
import { loadWindowsDownloadSecrets } from "./platform-download-secrets.mjs";

/** Capture host configuration once; requests cannot select a key, issuer or client. */
export function windowsNativeServiceFactory(options) {
    const configuration = Object.freeze({ ...options });
    const downloads = options.downloads === undefined ? undefined : Object.freeze({ ...options.downloads });
    return async ({ pool, authentication }) => {
        const identity = await loadWindowsNativeSecrets(configuration);
        let downloadKey;
        try {
            if (downloads) {
                downloadKey = await loadWindowsDownloadSecrets({
                    powerShellExecutable: configuration.powerShellExecutable,
                    decryptScriptPath: downloads.decryptScriptPath,
                    settingsDirectory: downloads.settingsDirectory,
                    siteOrigin: identity.siteOrigin,
                });
            }
            return createLocalNativeService({ ...identity, pool, authentication,
                downloadKey, packageStore: downloads?.packageStore });
        } finally {
            downloadKey?.fill(0);
        }
    };
}
