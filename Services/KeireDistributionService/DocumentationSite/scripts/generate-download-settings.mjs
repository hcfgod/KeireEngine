import { generateDownloadSettings } from "../Source/lib/platform-download-settings.mjs";

// Private setup subprocess: stdout must remain in the parent's protected-storage pipe.
try {
    const [siteOrigin, ...extra] = process.argv.slice(2);
    if (extra.length !== 0) {
        throw new Error();
    }
    process.stdout.write(JSON.stringify(generateDownloadSettings(siteOrigin)));
} catch {
    process.stderr.write("Download key setup failed.\n");
    process.exitCode = 1;
}
