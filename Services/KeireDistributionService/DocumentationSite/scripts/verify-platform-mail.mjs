import path from "node:path";
import { fileURLToPath } from "node:url";
import { createVerificationMailer, loadWindowsMailSecrets } from "../Source/lib/platform-mail.mjs";

let settings;
let mailer;
try {
    settings = await loadWindowsMailSecrets({
        powerShellExecutable: process.env.KEIRE_QA_POWERSHELL,
        decryptScriptPath: fileURLToPath(new URL("../../scripts/read-local-platform-mail.ps1", import.meta.url)),
        settingsDirectory: path.join(process.env.LOCALAPPDATA, "Keire", "PlatformMail"),
    });
    mailer = createVerificationMailer(settings);
    await mailer.verify();
    console.log(JSON.stringify({ smtpAuthentication: "passed", emailSent: false, liveWebsiteChanged: false }));
} catch {
    console.error("Email connection/authentication verification failed. No credentials were logged.");
    process.exitCode = 1;
} finally {
    mailer?.close();
    if (settings) {
        settings.password = null;
    }
}
