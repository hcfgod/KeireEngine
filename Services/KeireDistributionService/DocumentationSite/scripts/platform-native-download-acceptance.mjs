import assert from "node:assert/strict";
import { createHash, generateKeyPairSync, randomBytes, randomUUID } from "node:crypto";
import { mkdtemp, writeFile, unlink, rmdir } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { LocalBlobStore } from "../Source/lib/local-blob-store.ts";
import { issueNativeDownloadGrant } from "../Source/lib/platform-native-download-grants.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";
import { authorizeNativeDownloadDelivery } from "../Source/lib/platform-native-download-delivery.mjs";
import { createLocalNativeService } from "../Source/lib/platform-native-service.mjs";

/** Caller supplies only connection-local copies of all marketplace/native tables. */
export async function verifyNativeDownloadGrants(pool, connection, accessToken, clientId, userId, deviceSessionId) {
    const versionId = randomUUID();
    const productId = randomUUID();
    const entitlementId = randomUUID();
    const bytes = Buffer.from("synthetic immutable download package");
    const sha256 = createHash("sha256").update(bytes).digest("hex");
    const directory = await mkdtemp(path.join(tmpdir(), "keire-native-download-"));
    const file = path.join(directory, sha256);
    let fileExists = false;
    let service;
    let stage = "download-seed";
    try {
        await connection.execute(`INSERT INTO marketplace_product_versions
            (id,product_id,version,state,install_kind,minimum_engine_version,platforms,architectures,renderer_capabilities,
            release_notes_markdown,archive_storage_path,archive_sha256,archive_size_bytes,manifest_sha256,signature_key_id,
            created_at,updated_at,archive_storage_bucket)
            VALUES (?,?,'1.0.0','published','asset_import','0.4.7','[]','[]','[]','Fixture','fixture/package',?,?,?,?,
            UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),'marketplace-packages')`,
        [versionId, productId, sha256, bytes.length, "b".repeat(64), "fixture-key"]);
        await connection.execute(`INSERT INTO marketplace_publications
            (id,version_id,artifact_sha256,manifest_sha256,signature_key_id,signed_manifest,approved_by,published_at)
            VALUES (?,?,?,?,'fixture-key','synthetic-publication',?,UTC_TIMESTAMP(6))`,
        [randomUUID(), versionId, sha256, "b".repeat(64), userId]);
        await connection.execute(`INSERT INTO marketplace_entitlements
            (id,product_id,user_id,order_item_id,granted_at) VALUES (?,?,?,?,UTC_TIMESTAMP(6))`,
        [entitlementId, productId, userId, randomUUID()]);
        const store = new LocalBlobStore(directory);
        const input = { versionId, deviceSessionId };
        const issue = (packageStore = store) => issueNativeDownloadGrant(pool, packageStore, accessToken, clientId, input);
        await assert.rejects(issue(), { code: "marketplace.package_unavailable" });
        const [[empty]] = await connection.execute("SELECT COUNT(*) AS total FROM marketplace_download_grants");
        assert.equal(Number(empty.total), 0);
        await writeFile(file, bytes, { flag: "wx" });
        fileExists = true;
        const capabilityKey = randomBytes(32);
        try {
            const unexpectedBrowserCall = () => { throw new Error("Native download must not use browser credentials."); };
            service = createLocalNativeService({ pool, siteOrigin: "https://download.invalid", clientId,
                authentication: { session: unexpectedBrowserCall, withSessionTransaction: unexpectedBrowserCall },
                privateKey: generateKeyPairSync("ed25519").privateKey, downloadKey: capabilityKey, packageStore: store });
        } finally {
            capabilityKey.fill(0);
        }
        const handler = (request) => service.handle(request);
        stage = "download-http-issue";
        const issued = await handler(new Request("https://download.invalid/functions/v1/marketplace-hub", {
            method: "POST", headers: { authorization: `Bearer ${accessToken}`, "content-type": "application/json" },
            body: JSON.stringify({ operation: "download.issue", ...input }),
        }));
        assert.equal(issued.status, 201);
        const { data: grant } = await issued.json();
        const address = grant.url;
        stage = "download-http-status";
        const response = await handler(new Request(address));
        assert.equal(response.status, 200);
        stage = "download-http-bytes";
        assert.deepEqual(Buffer.from(await response.arrayBuffer()), bytes);
        stage = "download-http-tampering";
        const changedAddress = new URL(address);
        changedAddress.searchParams.set("signature", "a".repeat(43));
        assert.equal((await handler(new Request(changedAddress))).status, 403);
        const deliver = () => authorizeNativeDownloadDelivery(pool, grant.grant_id, clientId);
        stage = "download-revocations";
        assert.equal((await deliver()).archive_sha256, sha256);
        await assert.rejects(authorizeNativeDownloadDelivery(pool, grant.grant_id, "different-client"),
            { code: "marketplace.download_expired" });
        assert.equal(grant.archive_sha256, sha256);
        assert.equal(grant.archive_size_bytes, bytes.length);
        const [[saved]] = await connection.execute(`SELECT entitlement_id,session_id,
            TIMESTAMPDIFF(SECOND,created_at,expires_at) AS lifetime FROM marketplace_download_grants WHERE id=?`, [grant.grant_id]);
        assert.equal(saved.entitlement_id, entitlementId);
        assert.equal(saved.session_id, deviceSessionId);
        assert.equal(Number(saved.lifetime), 600);
        await connection.execute("UPDATE marketplace_entitlements SET revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [entitlementId]);
        await assert.rejects(issue(), { code: "marketplace.entitlement_required" });
        await assert.rejects(deliver(), { code: "marketplace.download_expired" });
        assert.equal((await handler(new Request(address))).status, 403);
        await connection.execute("UPDATE marketplace_entitlements SET revoked_at=NULL WHERE id=?", [entitlementId]);
        await connection.execute("UPDATE marketplace_product_versions SET security_revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [versionId]);
        await assert.rejects(issue(), { code: "marketplace.version_unavailable" });
        await assert.rejects(deliver(), { code: "marketplace.version_unavailable" });
        await connection.execute("UPDATE marketplace_product_versions SET security_revoked_at=NULL,state='withdrawn' WHERE id=?", [versionId]);
        assert.equal((await issue()).archive_sha256, sha256);
        assert.equal((await deliver()).archive_sha256, sha256);
        await connection.execute("UPDATE oauth_device_sessions SET revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [deviceSessionId]);
        await assert.rejects(deliver(), { code: "marketplace.download_expired" });
        await connection.execute("UPDATE oauth_device_sessions SET revoked_at=NULL WHERE id=?", [deviceSessionId]);
        const [[originalTimes]] = await connection.execute("SELECT created_at,expires_at FROM marketplace_download_grants WHERE id=?",
            [grant.grant_id]);
        await connection.execute(`UPDATE marketplace_download_grants
            SET created_at=TIMESTAMPADD(MINUTE,-20,UTC_TIMESTAMP(6)),expires_at=TIMESTAMPADD(MINUTE,-10,UTC_TIMESTAMP(6)) WHERE id=?`,
        [grant.grant_id]);
        await assert.rejects(deliver(), { code: "marketplace.download_expired" });
        await connection.execute("UPDATE marketplace_download_grants SET created_at=?,expires_at=? WHERE id=?",
            [originalTimes.created_at, originalTimes.expires_at, grant.grant_id]);
        await writeFile(file, Buffer.alloc(bytes.length));
        await assert.rejects(issue(), { code: "marketplace.package_unavailable" });
        const damagedResponse = await handler(new Request(address));
        assert.equal(damagedResponse.status, 503);
        assert.equal((await damagedResponse.json()).error.code, "marketplace.package_unavailable");
        await writeFile(file, bytes);
        await assert.rejects(issue({ verify: async (identity) => {
            await store.verify(identity);
            await connection.execute("UPDATE platform_native_access_tokens SET expires_ms=issued_ms+1 WHERE token_hash=?",
                [tokenDigest(accessToken)]);
        } }), { code: "account.session_expired" });
        const [[afterFailure]] = await connection.execute("SELECT COUNT(*) AS total FROM marketplace_download_grants");
        assert.equal(Number(afterFailure.total), 2);
        // The injected expiry was inside the failed grant transaction and must have rolled back as well.
        assert.equal((await issue()).archive_sha256, sha256);
        return 28;
    } catch (error) {
        error.fixtureStage = stage;
        throw error;
    } finally {
        await service?.close();
        if (fileExists) {
            await unlink(file);
        }
        await rmdir(directory);
    }
}
