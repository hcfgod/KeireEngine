import { randomUUID } from "node:crypto";
import { tokenDigest } from "./platform-auth-crypto.mjs";
import { LocalClaimError } from "./platform-claims.mjs";
import { withNativeSessionTransaction } from "./platform-native-sessions.mjs";
import { authorizeNativeDownload } from "./platform-native-download-access.mjs";

/** Issue only after hashing immutable package bytes. Delivery must recheck grant/session revocation. */
export async function issueNativeDownloadGrant(pool, packageStore, accessToken, clientId, input) {
    if (typeof packageStore?.verify !== "function") {
        throw new LocalClaimError(503, "marketplace.storage_unavailable", "Asset storage is temporarily unavailable.");
    }
    const result = await withNativeSessionTransaction(pool, accessToken, async (connection, identity) => {
        if (identity.clientId !== clientId) {
            throw new LocalClaimError(403, "account.invalid_client", "Sign in again from the registered Kéire Hub.");
        }
        const release = await authorizeNativeDownload(connection, identity, input);
        const size = Number(release.archive_size_bytes);
        if (!/^[0-9a-f]{64}$/.test(release.archive_sha256) || !Number.isSafeInteger(size) || size < 1 || size > 68719476736) {
            throw new LocalClaimError(503, "marketplace.package_invalid", "The verified package metadata is unavailable.");
        }
        try {
            await packageStore.verify({ sha256: release.archive_sha256, sizeBytes: size });
        } catch {
            throw new LocalClaimError(503, "marketplace.package_unavailable", "The verified package file is missing or damaged.");
        }
        // Hashing a large file can outlast the credential. Locked account/device/entitlement state cannot change here.
        const now = Date.now();
        const [[valid]] = await connection.execute(`SELECT access.expires_ms>? AND session.expires_ms>?
            AND session.revoked_ms IS NULL AS live FROM platform_native_access_tokens access
            JOIN platform_native_sessions session ON session.id=access.session_id
            WHERE access.token_hash=? AND session.id=?`, [now, now, tokenDigest(accessToken), identity.sessionId]);
        if (Number(valid?.live) !== 1) {
            throw new LocalClaimError(401, "account.session_expired", "Sign in again before downloading this asset.");
        }
        const grantId = randomUUID();
        await connection.execute(`INSERT INTO marketplace_download_grants
            (id,entitlement_id,version_id,session_id,created_at,expires_at)
            VALUES (?,?,?,?,UTC_TIMESTAMP(6),TIMESTAMPADD(MINUTE,10,UTC_TIMESTAMP(6)))`,
        [grantId, release.entitlementId, release.version_id, release.deviceSessionId]);
        const [[grant]] = await connection.execute(`SELECT DATE_FORMAT(expires_at,'%Y-%m-%dT%H:%i:%s.%fZ') AS expires_at
            FROM marketplace_download_grants WHERE id=?`, [grantId]);
        return { grant_id: grantId, storage_bucket: release.storage_bucket, storage_path: release.storage_path,
            archive_sha256: release.archive_sha256, archive_size_bytes: size,
            expires_at: grant.expires_at, signed_publication: release.signed_publication };
    });
    if (!result) {
        throw new LocalClaimError(401, "account.sign_in_required", "Sign in again to download this asset.");
    }
    return result;
}
