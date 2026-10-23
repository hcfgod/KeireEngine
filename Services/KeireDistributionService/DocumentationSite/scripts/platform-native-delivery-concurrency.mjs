import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { registerNativeDevice } from "../Source/lib/platform-native-devices.mjs";
import { authorizeNativeDownloadDelivery } from "../Source/lib/platform-native-download-delivery.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";

/** Shares only harness-owned session/device rows; grant, release, entitlement and flags are connection-local copies. */
export async function verifyNativeDeliveryConcurrency(pool, userId, accessToken, clientId) {
    const [[account]] = await pool.execute("SELECT email FROM platform_accounts WHERE id=?", [userId]);
    assert.equal(account?.email, `qa-${userId}@invalid.example`);
    const [[access]] = await pool.execute("SELECT session_id FROM platform_native_access_tokens WHERE token_hash=?", [tokenDigest(accessToken)]);
    assert.ok(access);
    const deviceId = randomUUID();
    const grantId = randomUUID();
    const versionId = randomUUID();
    const entitlementId = randomUUID();
    const productId = randomUUID();
    const connections = [];
    let deviceCreated = false;
    let registration;
    let delivery;
    let allowRegistration;
    let announceSession;
    let announceDelivery;
    const registrationHeld = new Promise((resolve) => { announceSession = resolve; });
    const deliveryWaiting = new Promise((resolve) => { announceDelivery = resolve; });
    const registrationRelease = new Promise((resolve) => { allowRegistration = resolve; });
    const deadline = (promise) => Promise.race([promise, new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("Native delivery concurrency barrier timed out.")), 10000);
        timer.unref();
        promise.finally(() => clearTimeout(timer)).catch(() => {});
    })]);
    const localTables = ["platform_feature_flags", "marketplace_download_grants", "marketplace_entitlements",
        "marketplace_product_versions", "marketplace_publications"];
    const pattern = new RegExp(`\\b(${localTables.join("|")})\\b`, "g");
    try {
        await pool.execute(`INSERT INTO oauth_device_sessions
            (id,user_id,oauth_session_id,client_type,device_name,created_at,last_used_at)
            VALUES (?,?,?,'hub','Concurrency QA',UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [deviceId, userId, `mysql:${access.session_id}`]);
        deviceCreated = true;
        connections.push(await pool.getConnection());
        connections.push(await pool.getConnection());
        assert.notEqual(connections[0].threadId, connections[1].threadId);
        for (const connection of connections) {
            for (const table of localTables) {
                await connection.execute(`CREATE TEMPORARY TABLE ${table}_qa_delivery LIKE ${table}`);
            }
            await connection.execute(`INSERT INTO platform_feature_flags_qa_delivery
                (\`key\`,enabled,description,updated_at) VALUES
                ('hub_oauth_sso_enabled',TRUE,'Concurrency QA',UTC_TIMESTAMP(6)),
                ('marketplace_enabled',TRUE,'Concurrency QA',UTC_TIMESTAMP(6)),
                ('asset_packages_enabled',TRUE,'Concurrency QA',UTC_TIMESTAMP(6))`);
        }
        const raw = connections[1];
        await raw.execute(`INSERT INTO marketplace_product_versions_qa_delivery
            (id,product_id,version,state,install_kind,minimum_engine_version,platforms,architectures,renderer_capabilities,
             release_notes_markdown,archive_storage_path,archive_sha256,archive_size_bytes,manifest_sha256,signature_key_id,
             created_at,updated_at,archive_storage_bucket)
            VALUES (?,?,'1.0.0','published','asset_import','0.4.7','[]','[]','[]','QA','qa/package',?,1,?,'qa-key',
                UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),'marketplace-packages')`, [versionId, productId, "a".repeat(64), "b".repeat(64)]);
        await raw.execute(`INSERT INTO marketplace_publications_qa_delivery
            (id,version_id,artifact_sha256,manifest_sha256,signature_key_id,signed_manifest,approved_by,published_at)
            VALUES (?,?,?,?,'qa-key','concurrency-only',?,UTC_TIMESTAMP(6))`,
        [randomUUID(), versionId, "a".repeat(64), "b".repeat(64), userId]);
        await raw.execute(`INSERT INTO marketplace_entitlements_qa_delivery
            (id,product_id,user_id,order_item_id,granted_at) VALUES (?,?,?,?,UTC_TIMESTAMP(6))`,
        [entitlementId, productId, userId, randomUUID()]);
        await raw.execute(`INSERT INTO marketplace_download_grants_qa_delivery
            (id,entitlement_id,version_id,session_id,created_at,expires_at)
            VALUES (?,?,?,?,UTC_TIMESTAMP(6),TIMESTAMPADD(MINUTE,5,UTC_TIMESTAMP(6)))`,
        [grantId, entitlementId, versionId, deviceId]);
        function wrapper(connection, kind) {
            return {
                async execute(sql, values) {
                    const sessionLock = sql.includes("platform_native_sessions WHERE id=? FOR UPDATE");
                    if (kind === "delivery" && sessionLock) {
                        announceDelivery();
                    }
                    const result = await connection.execute(sql.replace(pattern, "$1_qa_delivery"), values);
                    if (kind === "registration" && sessionLock) {
                        announceSession();
                        await deadline(registrationRelease);
                    }
                    return result;
                },
                beginTransaction: () => connection.beginTransaction(),
                commit: () => connection.commit(),
                rollback: () => connection.rollback(),
                release() {},
                destroy: () => connection.destroy(),
            };
        }
        registration = registerNativeDevice({ getConnection: async () => wrapper(connections[0], "registration") },
            accessToken, clientId, "Concurrent refreshed device");
        registration.catch(() => {});
        await deadline(registrationHeld);
        delivery = authorizeNativeDownloadDelivery({ getConnection: async () => wrapper(connections[1], "delivery") }, grantId, clientId);
        delivery.catch(() => {});
        await deadline(deliveryWaiting);
        // Registration owns the session; delivery is waiting for that session and must not own the device.
        allowRegistration();
        const [registered, downloaded] = await deadline(Promise.all([registration, delivery]));
        assert.equal(registered.deviceSessionId, deviceId);
        assert.equal(downloaded.entitlementId, entitlementId);
        assert.equal(downloaded.archive_sha256, "a".repeat(64));
        return 5;
    } finally {
        allowRegistration();
        // Destroy only these held physical connections; their temporary tables disappear automatically.
        for (const connection of connections) {
            connection.destroy();
        }
        await Promise.allSettled([registration, delivery]);
        if (deviceCreated) {
            await pool.execute("DELETE FROM oauth_device_sessions WHERE id=? AND user_id=?", [deviceId, userId]);
        }
    }
}
