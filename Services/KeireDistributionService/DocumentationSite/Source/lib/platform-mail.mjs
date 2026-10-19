import { execFile } from "node:child_process";
import path from "node:path";
import { promisify } from "node:util";
import nodemailer from "nodemailer";

const execute = promisify(execFile);
const purposes = Object.freeze({
    sign_in: "sign in to your Kéire account",
    enroll: "enable email verification on your Kéire account",
});

function validAddress(value) {
    return typeof value === "string" && value.length <= 254 &&
        /^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?$/.test(value);
}

function validateSettings(settings) {
    if (settings?.schemaVersion !== 1 || settings.provider !== "gmail" ||
        settings.host !== "smtp.gmail.com" || settings.port !== 465 || settings.secure !== true ||
        !validAddress(settings.username) || settings.fromAddress !== settings.username ||
        typeof settings.password !== "string" || !/^[A-Za-z0-9]{16}$/.test(settings.password)) {
        throw new Error("Protected email settings are invalid.");
    }
}

/** Bootstrap only. Never log the returned settings or child-process error output. */
export async function loadWindowsMailSecrets({ powerShellExecutable, decryptScriptPath, settingsDirectory }) {
    for (const value of [powerShellExecutable, decryptScriptPath, settingsDirectory]) {
        if (typeof value !== "string" || !path.isAbsolute(value)) {
            throw new Error("Protected email loader paths must be absolute.");
        }
    }
    try {
        const result = await execute(powerShellExecutable, [
            "-NoLogo", "-NoProfile", "-NonInteractive", "-File", decryptScriptPath,
            "-SettingsDirectory", settingsDirectory,
        ], { shell: false, windowsHide: true, timeout: 15000, maxBuffer: 16384, encoding: "utf8" });
        const settings = JSON.parse(result.stdout.replace(/^\uFEFF/, ""));
        validateSettings(settings);
        return settings;
    } catch {
        throw new Error("Protected email settings are unavailable.");
    }
}

/** Application-owned transport. Callers must authorize and rate-limit code requests before sending. */
export function createVerificationMailer(settings, createTransport = nodemailer.createTransport, recoveryOrigin = null) {
    validateSettings(settings);
    if (recoveryOrigin !== null) {
        const origin = new URL(recoveryOrigin);
        if (origin.protocol !== "https:" || origin.origin !== recoveryOrigin) {
            throw new Error("Recovery email requires a canonical HTTPS origin.");
        }
    }
    const from = settings.fromAddress;
    const transport = createTransport({
        host: "smtp.gmail.com",
        port: 465,
        secure: true,
        auth: { user: settings.username, pass: settings.password },
        tls: { rejectUnauthorized: true, minVersion: "TLSv1.2", servername: "smtp.gmail.com" },
        connectionTimeout: 10000,
        greetingTimeout: 10000,
        socketTimeout: 15000,
        disableFileAccess: true,
        disableUrlAccess: true,
        logger: false,
        debug: false,
    });
    let closed = false;

    function requireOpen() {
        if (closed) {
            throw new Error("Email delivery is closed.");
        }
    }

    return Object.freeze({
        recoveryEnabled: recoveryOrigin !== null,
        async verify() {
            requireOpen();
            try {
                await transport.verify();
            } catch {
                throw new Error("Email delivery credentials or connection could not be verified.");
            }
        },

        async sendVerification({ recipient, code, purpose }) {
            requireOpen();
            if (!validAddress(recipient) || typeof code !== "string" || !/^[0-9]{8}$/.test(code) ||
                !Object.hasOwn(purposes, purpose)) {
                throw new Error("Invalid verification email request.");
            }
            try {
                const result = await transport.sendMail({
                    from: { name: "Kéire", address: from },
                    to: { address: recipient },
                    envelope: { from, to: [recipient] },
                    subject: "Your Kéire verification code",
                    text: `Your code to ${purposes[purpose]} is: ${code}\n\n` +
                        "This code expires in 5 minutes and can only be used once.\n" +
                        "If you did not request this code, you can ignore this email.\n" +
                        "Never share this code with anyone.",
                    disableFileAccess: true,
                    disableUrlAccess: true,
                });
                if (result.accepted?.length !== 1 || result.rejected?.length > 0) {
                    throw new Error("Recipient was not accepted.");
                }
            } catch {
                // SMTP errors can contain addresses, server responses and credential-bearing context.
                // Do not retry here: an uncertain delivery must not create multiple valid challenges.
                throw new Error("The verification email could not be sent. Please try again later.");
            }
        },

        async sendRecovery({ recipient, token }) {
            requireOpen();
            if (!recoveryOrigin || !validAddress(recipient) || typeof token !== "string" ||
                !/^[A-Za-z0-9_-]{43}$/.test(token)) {
                throw new Error("Invalid recovery email request.");
            }
            const link = `${recoveryOrigin}/account/update-password/#recovery=${token}`;
            try {
                const result = await transport.sendMail({
                    from: { name: "Kéire", address: from },
                    to: { address: recipient },
                    envelope: { from, to: [recipient] },
                    subject: "Reset your Kéire password",
                    text: `Open this link to choose a new password:\n\n${link}\n\n` +
                        "This link expires in 15 minutes and can only be used once.\n" +
                        "If you have an authenticator enrolled, you will still need its code.\n" +
                        "If you did not request a password reset, you can ignore this email.\n" +
                        "Never share this link with anyone.",
                    disableFileAccess: true,
                    disableUrlAccess: true,
                });
                if (result.accepted?.length !== 1 || result.rejected?.length > 0) {
                    throw new Error("Recipient was not accepted.");
                }
            } catch {
                throw new Error("The recovery email could not be sent. Please try again later.");
            }
        },

        close() {
            if (!closed) {
                closed = true;
                transport.close();
            }
        },
    });
}
