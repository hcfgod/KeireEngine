import { createHash, createPublicKey } from "node:crypto";
import { SignJWT } from "jose";

const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const identityLifetimeSeconds = 300;

/** The host loads a dedicated protected Ed25519 key; no runtime-generated production signing keys. */
export function createNativeIdentitySigner({ siteOrigin, clientId, privateKey }) {
    const origin = new URL(siteOrigin);
    if (origin.protocol !== "https:" || origin.origin !== siteOrigin
        || typeof clientId !== "string" || !/^[A-Za-z0-9._~-]{8,128}$/.test(clientId)
        || privateKey?.type !== "private" || privateKey.asymmetricKeyType !== "ed25519") {
        throw new Error("Native identity signing requires a registered HTTPS issuer and a protected Ed25519 key.");
    }
    const issuer = `${siteOrigin}/auth/v1`;
    const publicKey = createPublicKey(privateKey).export({ format: "jwk" });
    const fingerprint = createHash("sha256").update(Buffer.from(publicKey.x, "base64url")).digest("hex");
    const keyId = `native-ed25519-${fingerprint.slice(0, 32)}`;
    const verificationKey = Object.freeze({ ...publicKey, kid: keyId, alg: "EdDSA", use: "sig" });
    let key = privateKey;
    return Object.freeze({
        issuer,
        clientId,
        jwks: Object.freeze({ keys: Object.freeze([verificationKey]) }),
        close() {
            key = null;
        },
        async sign({ userId, sessionId, nonce, assurance, authenticatedAt, scope,
            email, emailVerified, displayName, accessToken, now = Date.now() }) {
            if (!key) {
                throw new Error("Native identity signing is closed.");
            }
            const scopes = typeof scope === "string" ? scope.split(" ") : [];
            if (!uuidPattern.test(userId ?? "") || !uuidPattern.test(sessionId ?? "")
                || typeof nonce !== "string" || !/^[A-Za-z0-9_-]{32,128}$/.test(nonce)
                || ![1, 2].includes(assurance) || !Number.isSafeInteger(now) || now < 0
                || !Number.isSafeInteger(authenticatedAt) || authenticatedAt < 0 || authenticatedAt > now
                || !scopes.includes("openid") || new Set(scopes).size !== scopes.length
                || scopes.some((item) => !["openid", "email", "profile"].includes(item))
                || typeof accessToken !== "string" || !/^[A-Za-z0-9_-]{43}$/.test(accessToken)) {
                throw new Error("Invalid native identity claims.");
            }
            const claims = {
                nonce,
                sid: sessionId,
                auth_time: Math.floor(authenticatedAt / 1000),
                acr: assurance === 2 ? "urn:keire:assurance:2" : "urn:keire:assurance:1",
                // OpenID's EdDSA/Ed25519 token binding uses the left half of SHA-512.
                at_hash: createHash("sha512").update(accessToken, "ascii").digest().subarray(0, 32).toString("base64url"),
            };
            if (scopes.includes("email")) {
                if (typeof email !== "string" || email.length < 3 || email.length > 320
                    || typeof emailVerified !== "boolean") {
                    throw new Error("Invalid native email claims.");
                }
                claims.email = email;
                claims.email_verified = emailVerified;
            }
            if (scopes.includes("profile") && displayName != null) {
                if (typeof displayName !== "string" || displayName.length > 256) {
                    throw new Error("Invalid native profile claims.");
                }
                claims.name = displayName;
            }
            const issuedAt = Math.floor(now / 1000);
            return new SignJWT(claims)
                .setProtectedHeader({ alg: "EdDSA", kid: keyId, typ: "JWT" })
                .setIssuer(issuer)
                .setAudience(clientId)
                .setSubject(userId)
                .setIssuedAt(issuedAt)
                .setExpirationTime(issuedAt + identityLifetimeSeconds)
                .sign(key);
        },
    });
}
