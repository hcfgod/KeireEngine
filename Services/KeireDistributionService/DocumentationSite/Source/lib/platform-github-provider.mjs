import { createHash } from "node:crypto";

const maximumResponseBytes = 65536;
const tokenPattern = /^[A-Za-z0-9_-]{43}$/;

export function githubConfiguration(settings) {
    if (!settings || typeof settings !== "object" || Array.isArray(settings) ||
        typeof settings.clientId !== "string" || !/^[A-Za-z0-9_.-]{10,128}$/.test(settings.clientId) ||
        !Buffer.isBuffer(settings.clientSecret) || settings.clientSecret.length < 20 || settings.clientSecret.length > 512) {
        throw new Error("GitHub sign-in requires protected OAuth application credentials.");
    }
    const origin = new URL(settings.siteOrigin);
    if (origin.protocol !== "https:" || origin.origin !== settings.siteOrigin) {
        throw new Error("GitHub sign-in requires a canonical HTTPS site origin.");
    }
    return { clientId: settings.clientId, siteOrigin: origin.origin,
        callback: `${origin.origin}/account/github/callback/` };
}

async function readProviderJson(response) {
    if (response.status !== 200 || response.headers.get("content-type")?.split(";")[0].trim() !== "application/json") {
        await response.body?.cancel();
        throw new Error("GitHub could not verify this sign-in. Start again.");
    }
    const reader = response.body?.getReader();
    if (!reader) {
        throw new Error("GitHub returned an empty response.");
    }
    const chunks = [];
    let length = 0;
    try {
        while (true) {
            const part = await reader.read();
            if (part.done) {
                break;
            }
            length += part.value.byteLength;
            if (length > maximumResponseBytes) {
                await reader.cancel();
                throw new Error("GitHub returned an oversized response.");
            }
            chunks.push(part.value);
        }
        return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(Buffer.concat(chunks)));
    } finally {
        reader.releaseLock();
    }
}

/** Owns a copy of the secret. Fixed GitHub endpoints never follow redirects with credentials. */
export function createGitHubProvider(settings, fetcher = fetch) {
    const configuration = githubConfiguration(settings);
    const secret = Buffer.from(settings.clientSecret);
    let closed = false;
    const active = new Set();
    function ensureOpen() {
        if (closed) {
            throw new Error("GitHub sign-in is closed.");
        }
    }
    async function request(url, options, signal) {
        const response = await fetcher(url, { ...options, redirect: "error", signal });
        return readProviderJson(response);
    }
    return {
        ...configuration,
        authorize(state, verifier) {
            ensureOpen();
            if (!tokenPattern.test(state) || !tokenPattern.test(verifier)) {
                throw new Error("Invalid GitHub authorization state.");
            }
            const url = new URL("https://github.com/login/oauth/authorize");
            url.search = new URLSearchParams({ client_id: configuration.clientId, redirect_uri: configuration.callback,
                scope: "read:user user:email", state, code_challenge_method: "S256",
                code_challenge: createHash("sha256").update(verifier).digest("base64url") }).toString();
            return url.toString();
        },
        async identity(code, verifier) {
            ensureOpen();
            if (typeof code !== "string" || !/^[A-Za-z0-9_-]{1,512}$/.test(code) || !tokenPattern.test(verifier)) {
                throw new Error("Invalid GitHub authorization code.");
            }
            const controller = new AbortController();
            active.add(controller);
            const timeout = setTimeout(() => controller.abort(), 15000);
            try {
                const token = await request("https://github.com/login/oauth/access_token", {
                    method: "POST", headers: { accept: "application/json", "content-type": "application/x-www-form-urlencoded" },
                    body: new URLSearchParams({ client_id: configuration.clientId, client_secret: secret.toString("utf8"),
                        code, redirect_uri: configuration.callback, code_verifier: verifier }).toString(),
                }, controller.signal);
                if (token.error || token.token_type?.toLowerCase() !== "bearer" || typeof token.access_token !== "string" ||
                    !/^[A-Za-z0-9_]{10,512}$/.test(token.access_token)) {
                    throw new Error("GitHub did not return a valid access token.");
                }
                const headers = { accept: "application/vnd.github+json", "user-agent": "Keire-Account-Service",
                    "x-github-api-version": "2022-11-28", authorization: `Bearer ${token.access_token}` };
                const user = await request("https://api.github.com/user", { headers }, controller.signal);
                const emails = await request("https://api.github.com/user/emails?per_page=100", { headers }, controller.signal);
                if (!Number.isSafeInteger(user.id) || user.id <= 0 || typeof user.login !== "string" ||
                    !/^[A-Za-z0-9-]{1,39}$/.test(user.login) || !Array.isArray(emails)) {
                    throw new Error("GitHub identity is invalid.");
                }
                const email = emails.find((entry) => entry?.primary === true && entry.verified === true &&
                    typeof entry.email === "string" && entry.email.length <= 320 &&
                    /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(entry.email));
                if (!email) {
                    throw new Error("Verify your primary email address on GitHub before signing in.");
                }
                ensureOpen();
                return { subject: String(user.id), login: user.login, email: email.email };
            } catch {
                // Never include provider bodies, tokens, codes, or fetch diagnostics in application logs.
                throw new Error("GitHub sign-in could not be verified. Check your GitHub email and try again.");
            } finally {
                clearTimeout(timeout);
                active.delete(controller);
            }
        },
        close() {
            closed = true;
            secret.fill(0);
            for (const controller of active) {
                controller.abort();
            }
        },
    };
}
