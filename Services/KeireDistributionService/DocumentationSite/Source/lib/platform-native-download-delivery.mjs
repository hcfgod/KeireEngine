import { platformTransaction } from "./platform-mysql.mjs";
import { accountAvailable } from "./platform-local-auth.mjs";
import { nativeAccountBinding } from "./platform-native-authorization.mjs";
import { authorizeNativeDownload } from "./platform-native-download-access.mjs";
import { LocalClaimError } from "./platform-claims.mjs";

function denied() {
    return new LocalClaimError(403, "marketplace.download_expired", "This download link expired or access was revoked. Request a new download from Kéire Hub.");
}

/** Call only after cryptographic link verification. A grant ID alone is not a credential. */
export async function authorizeNativeDownloadDelivery(pool, grantId, clientId, clock = Date.now) {
    return platformTransaction(pool, async (connection) => {
        const [[grant]] = await connection.execute(`SELECT grant_record.id,grant_record.version_id,grant_record.session_id AS device_id,
            grant_record.entitlement_id,entitlement.organization_id,native.id AS native_session_id,native.user_id,
            native.client_id,native.account_binding,native.expires_ms,native.revoked_ms,
            grant_record.expires_at>UTC_TIMESTAMP(6) AS live
            FROM marketplace_download_grants grant_record
            JOIN marketplace_entitlements entitlement ON entitlement.id=grant_record.entitlement_id AND entitlement.revoked_at IS NULL
            JOIN oauth_device_sessions device ON device.id=grant_record.session_id AND device.revoked_at IS NULL
            JOIN platform_native_sessions native ON device.oauth_session_id=CONCAT('mysql:',native.id)
                AND device.user_id=native.user_id AND device.client_type='hub'
            WHERE grant_record.id=? FOR UPDATE`, [grantId]);
        if (!grant || Number(grant.live) !== 1 || grant.revoked_ms !== null || grant.client_id !== clientId
            || Number(grant.expires_ms) <= clock()) {
            throw denied();
        }
        const [[account]] = await connection.execute("SELECT * FROM platform_accounts WHERE id=? FOR UPDATE", [grant.user_id]);
        const [factors] = await connection.execute("SELECT * FROM platform_mfa_factors WHERE user_id=? AND status='verified' FOR UPDATE",
            [grant.user_id]);
        if (!accountAvailable(account, clock()) || nativeAccountBinding(account, factors) !== grant.account_binding) {
            throw denied();
        }
        const release = await authorizeNativeDownload(connection, { userId: grant.user_id, sessionId: grant.native_session_id }, {
            versionId: grant.version_id, deviceSessionId: grant.device_id, organizationId: grant.organization_id,
        });
        if (release.entitlementId !== grant.entitlement_id) {
            throw denied();
        }
        const [[current]] = await connection.execute("SELECT expires_at>UTC_TIMESTAMP(6) AS live FROM marketplace_download_grants WHERE id=?",
            [grantId]);
        if (Number(current?.live) !== 1 || Number(grant.expires_ms) <= clock()) {
            throw denied();
        }
        return release;
    });
}
