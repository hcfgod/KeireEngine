import { createHash, timingSafeEqual } from "node:crypto";

export class NativeOAuthError extends Error {
    constructor(code, message) {
        super(message);
        this.code = code;
    }
}

function reject(code = "invalid_request") {
    throw new NativeOAuthError(code, "The desktop authorization request is invalid or unsupported.");
}

/** Configuration is host-owned. Never accept a redirect URI or client registration from a request. */
export function createNativeOAuthPolicy({ siteOrigin, clientId }) {
    const origin = new URL(siteOrigin);
    if (origin.protocol !== "https:" || origin.origin !== siteOrigin
        || typeof clientId !== "string" || !/^[A-Za-z0-9._~-]{8,128}$/.test(clientId)) {
        throw new Error("Native authorization requires a canonical HTTPS origin and a registered client.");
    }
    const redirectUri = `${siteOrigin}/oauth/hub/callback/`;
    const allowed = new Set(["response_type", "client_id", "redirect_uri", "scope", "state", "nonce",
        "code_challenge", "code_challenge_method"]);
    return Object.freeze({
        clientId,
        redirectUri,
        authorize(parameters) {
            if (!(parameters instanceof URLSearchParams) || parameters.toString().length > 4096) {
                return reject();
            }
            for (const key of parameters.keys()) {
                if (!allowed.has(key) || parameters.getAll(key).length !== 1) {
                    return reject();
                }
            }
            if (parameters.get("client_id") !== clientId || parameters.get("redirect_uri") !== redirectUri) {
                return reject();
            }
            if (parameters.get("response_type") !== "code") {
                return reject("unsupported_response_type");
            }
            const scope = parameters.get("scope") ?? "";
            const scopes = scope.split(" ");
            if (!scopes.includes("openid") || scopes.some((item) => !["openid", "email", "profile"].includes(item))
                || new Set(scopes).size !== scopes.length) {
                return reject("invalid_scope");
            }
            const state = parameters.get("state");
            const nonce = parameters.get("nonce");
            const challenge = parameters.get("code_challenge");
            if (!/^[A-Za-z0-9_-]{32,128}$/.test(state ?? "") || !/^[A-Za-z0-9_-]{32,128}$/.test(nonce ?? "")
                || parameters.get("code_challenge_method") !== "S256"
                || !/^[A-Za-z0-9_-]{43}$/.test(challenge ?? "")
                || Buffer.from(challenge, "base64url").toString("base64url") !== challenge) {
                return reject();
            }
            return Object.freeze({ clientId, redirectUri, scope, state, nonce, challenge });
        },
    });
}

/** Compare only after locking a live, single-use code bound to this client and redirect URI. */
export function verifyNativePkce(challenge, verifier) {
    if (typeof verifier !== "string" || !/^[A-Za-z0-9._~-]{43,128}$/.test(verifier)
        || typeof challenge !== "string" || !/^[A-Za-z0-9_-]{43}$/.test(challenge)) {
        return false;
    }
    const actual = createHash("sha256").update(verifier, "ascii").digest("base64url");
    return timingSafeEqual(Buffer.from(actual, "ascii"), Buffer.from(challenge, "ascii"));
}
