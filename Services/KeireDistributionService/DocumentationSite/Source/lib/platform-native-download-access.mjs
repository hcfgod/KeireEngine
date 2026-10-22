import { LocalClaimError } from "./platform-claims.mjs";

const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

function reject(status, code, message) {
    throw new LocalClaimError(status, `marketplace.${code}`, message);
}

/** Call inside withNativeSessionTransaction. Locks survive until grant issuance or rollback. */
export async function authorizeNativeDownload(connection, identity, input) {
    const { versionId, deviceSessionId, organizationId = null } = input;
    if (!uuid.test(versionId ?? "") || !uuid.test(deviceSessionId ?? "")
        || (organizationId !== null && !uuid.test(organizationId))) {
        reject(400, "invalid_download", "The requested asset, device or organization is invalid.");
    }
    const [flags] = await connection.execute("SELECT `key`,enabled FROM platform_feature_flags "
        + "WHERE `key` IN ('marketplace_enabled','asset_packages_enabled','hub_oauth_sso_enabled') FOR SHARE");
    if (flags.length !== 3 || flags.some((flag) => Number(flag.enabled) !== 1)) {
        reject(503, "unavailable", "Asset downloads are temporarily unavailable.");
    }
    const [[device]] = await connection.execute(`SELECT id FROM oauth_device_sessions
        WHERE id=? AND user_id=? AND oauth_session_id=? AND client_type='hub' AND revoked_at IS NULL FOR SHARE`,
    [deviceSessionId, identity.userId, `mysql:${identity.sessionId}`]);
    if (!device) {
        reject(403, "device_session_invalid", "This device session is unavailable. Sign in again from Kéire Hub.");
    }
    const [[release]] = await connection.execute(`SELECT version.id AS version_id,version.product_id,
        version.archive_storage_bucket AS storage_bucket,version.archive_storage_path AS storage_path,
        version.archive_sha256,CAST(version.archive_size_bytes AS CHAR) AS archive_size_bytes,
        version.manifest_sha256,version.signature_key_id,publication.signed_manifest AS signed_publication
        FROM marketplace_product_versions version
        JOIN marketplace_publications publication ON publication.version_id=version.id
            AND publication.artifact_sha256=version.archive_sha256 AND publication.manifest_sha256=version.manifest_sha256
            AND publication.signature_key_id=version.signature_key_id
        WHERE version.id=? AND version.state IN ('published','withdrawn') AND version.security_revoked_at IS NULL
            AND version.archive_storage_path IS NOT NULL AND publication.signed_manifest IS NOT NULL FOR SHARE`, [versionId]);
    if (!release) {
        reject(404, "version_unavailable", "This asset version is unavailable or was revoked for security reasons.");
    }
    if (organizationId !== null) {
        const [[membership]] = await connection.execute(`SELECT membership.user_id FROM organization_memberships membership
            JOIN organizations organization ON organization.id=membership.organization_id
            WHERE membership.organization_id=? AND membership.user_id=? AND organization.deleted_at IS NULL FOR SHARE`,
        [organizationId, identity.userId]);
        if (!membership) {
            reject(403, "organization_forbidden", "You no longer have access to this organization's assets.");
        }
    }
    const ownerField = organizationId === null ? "user_id" : "organization_id";
    const [[entitlement]] = await connection.execute(`SELECT id FROM marketplace_entitlements
        WHERE product_id=? AND ${ownerField}=? AND revoked_at IS NULL FOR SHARE`,
    [release.product_id, organizationId ?? identity.userId]);
    if (!entitlement) {
        reject(403, "entitlement_required", "Add this asset to your library before downloading it.");
    }
    return { ...release, entitlementId: entitlement.id, deviceSessionId };
}
