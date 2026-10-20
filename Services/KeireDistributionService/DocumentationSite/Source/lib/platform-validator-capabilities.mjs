import { createHmac, timingSafeEqual } from "node:crypto";
import { ValidatorQueueError } from "./platform-validator-error.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

function denied() {
    return new ValidatorQueueError(403, "validator.transfer_denied", "The validator file link is invalid or expired.");
}

function validClaims(claims) {
    return claims?.version === 1
        && typeof claims.uploadId === "string"
        && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(claims.uploadId)
        && typeof claims.workerId === "string" && /^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$/.test(claims.workerId)
        && Number.isInteger(claims.attempt) && claims.attempt >= 1 && claims.attempt <= 5
        && ["package", "evidence"].includes(claims.purpose);
}

/** Links follow the current DB lease lifetime, including renewal, but never survive reassignment. */
export function createValidatorCapabilities(secret, siteOrigin) {
    if (!Buffer.isBuffer(secret) || secret.length !== 32) {
        throw new Error("Validator file links require a dedicated 32-byte signing key.");
    }
    const origin = new URL(siteOrigin);
    if (origin.protocol !== "https:" || origin.origin !== siteOrigin) {
        throw new Error("Validator file links require a canonical HTTPS origin.");
    }
    const key = Buffer.from(secret);
    let closed = false;
    const signature = (payload) => createHmac("sha256", key)
        .update("Keire.ValidatorTransfer.v1\0").update(payload).digest();
    return Object.freeze({
        close() {
            closed = true;
            key.fill(0);
        },
        issue({ uploadId, workerId, attempt, purpose }) {
            const claims = { version: 1, uploadId, workerId, attempt, purpose };
            if (closed || !validClaims(claims)) {
                throw denied();
            }
            const payload = Buffer.from(JSON.stringify(claims)).toString("base64url");
            const token = `${payload}.${signature(payload).toString("base64url")}`;
            return `${siteOrigin}/internal/validator/v1/files/${uploadId}/${purpose}/?token=${token}`;
        },
        verify(address, method) {
            if (closed || typeof address !== "string" || address.length > 2048) {
                throw denied();
            }
            let url;
            try {
                url = new URL(address);
            } catch {
                throw denied();
            }
            const tokens = url.searchParams.getAll("token");
            const [payload, supplied, extra] = (tokens[0] ?? "").split(".");
            if (url.origin !== siteOrigin || url.username || url.password || url.hash
                || tokens.length !== 1 || [...url.searchParams.keys()].length !== 1 || extra !== undefined
                || !/^[A-Za-z0-9_-]{1,1024}$/.test(payload ?? "")
                || !/^[A-Za-z0-9_-]{43}$/.test(supplied ?? "")) {
                throw denied();
            }
            const suppliedBytes = Buffer.from(supplied, "base64url");
            if (suppliedBytes.toString("base64url") !== supplied
                || !timingSafeEqual(signature(payload), suppliedBytes)) {
                throw denied();
            }
            let claims;
            try {
                claims = JSON.parse(Buffer.from(payload, "base64url").toString("utf8"));
            } catch {
                throw denied();
            }
            if (!validClaims(claims)
                || url.pathname !== `/internal/validator/v1/files/${claims.uploadId}/${claims.purpose}/`
                || method !== (claims.purpose === "package" ? "GET" : "PUT")) {
                throw denied();
            }
            return Object.freeze(claims);
        },
    });
}

/** Mandatory after signature verification and before every transfer, never cache this decision. */
export async function authorizeValidatorTransfer(pool, claims) {
    if (!validClaims(claims)) {
        throw denied();
    }
    return platformTransaction(pool, async (connection) => {
        const [[upload]] = await connection.execute(`SELECT upload.id,upload.version_id,upload.storage_bucket,
            upload.storage_path,upload.expected_sha256,upload.expected_size_bytes
            FROM marketplace_uploads upload JOIN marketplace_product_versions version ON version.id=upload.version_id
            WHERE upload.id=? AND upload.lease_owner=? AND upload.validation_attempts=?
            AND upload.state='leased' AND upload.lease_expires_at>UTC_TIMESTAMP(6)
            AND version.state='validating' FOR SHARE`,
        [claims.uploadId, claims.workerId, claims.attempt]);
        if (!upload) {
            throw denied();
        }
        return upload;
    });
}
