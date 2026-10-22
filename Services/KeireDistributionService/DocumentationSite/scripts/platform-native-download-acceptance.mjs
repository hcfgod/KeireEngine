import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { mkdtemp, writeFile, unlink, rmdir } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { LocalBlobStore } from "../Source/lib/local-blob-store.ts";
import { issueNativeDownloadGrant } from "../Source/lib/platform-native-download-grants.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";

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
        const grant = await issue();
        assert.equal(grant.archive_sha256, sha256);
        assert.equal(grant.archive_size_bytes, bytes.length);
        const [[saved]] = await connection.execute(`SELECT entitlement_id,session_id,
            TIMESTAMPDIFF(SECOND,created_at,expires_at) AS lifetime FROM marketplace_download_grants WHERE id=?`, [grant.grant_id]);
        assert.equal(saved.entitlement_id, entitlementId);
        assert.equal(saved.session_id, deviceSessionId);
        assert.equal(Number(saved.lifetime), 600);
        await connection.execute("UPDATE marketplace_entitlements SET revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [entitlementId]);
        await assert.rejects(issue(), { code: "marketplace.entitlement_required" });
        await connection.execute("UPDATE marketplace_entitlements SET revoked_at=NULL WHERE id=?", [entitlementId]);
        await connection.execute("UPDATE marketplace_product_versions SET security_revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [versionId]);
        await assert.rejects(issue(), { code: "marketplace.version_unavailable" });
        await connection.execute("UPDATE marketplace_product_versions SET security_revoked_at=NULL,state='withdrawn' WHERE id=?", [versionId]);
        assert.equal((await issue()).archive_sha256, sha256);
        await writeFile(file, Buffer.alloc(bytes.length));
        await assert.rejects(issue(), { code: "marketplace.package_unavailable" });
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
        return 14;
    } finally {
        if (fileExists) {
            await unlink(file);
        }
        await rmdir(directory);
    }
}
