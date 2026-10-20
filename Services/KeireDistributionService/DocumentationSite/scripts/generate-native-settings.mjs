import { generateNativeSettings } from "../Source/lib/platform-native-settings.mjs";

// Private setup subprocess: stdout contains key material and must remain in the parent's pipe.
try {
    const [siteOrigin, clientId, ...extra] = process.argv.slice(2);
    if (extra.length !== 0) {
        throw new Error();
    }
    process.stdout.write(JSON.stringify(generateNativeSettings(siteOrigin, clientId)));
} catch {
    process.stderr.write("Native identity setup failed.\n");
    process.exitCode = 1;
}
