import { loadWindowsNativeSecrets } from "./platform-native-secrets.mjs";
import { createLocalNativeService } from "./platform-native-service.mjs";

/** Capture host configuration once; requests cannot select a key, issuer or client. */
export function windowsNativeServiceFactory(options) {
    const configuration = Object.freeze({ ...options });
    return async ({ pool, authentication }) => {
        const identity = await loadWindowsNativeSecrets(configuration);
        return createLocalNativeService({ ...identity, pool, authentication });
    };
}
