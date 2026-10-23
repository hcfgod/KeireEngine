import assert from "node:assert/strict";
import { createHash, randomBytes, randomUUID } from "node:crypto";
import { writeFile, unlink } from "node:fs/promises";
import path from "node:path";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

/** Private transport-only fixture; never represents an installable signed engine package. */
export async function createDownloadHttpFixture(pool, userId, directory) {
    const [[account]] = await pool.execute("SELECT email_key FROM platform_accounts WHERE id=?", [userId]);
    assert.equal(account?.email_key, `http-qa-${userId}@invalid.example`);
    assert.ok(path.isAbsolute(directory));
    const ids = Object.fromEntries(["organization", "publisher", "product", "version", "publication", "offer", "order", "item", "entitlement"]
        .map((name) => [name, randomUUID()]));
    const bytes = Buffer.concat([Buffer.from("Synthetic HTTPS transport fixture; not an asset package.\n"), randomBytes(4096)]);
    const sha256 = createHash("sha256").update(bytes).digest("hex");
    const file = path.join(directory, sha256);
    let fileCreated = false;
    async function close() {
        await platformTransaction(pool, async (connection) => {
            await connection.execute("DELETE FROM marketplace_download_grants WHERE entitlement_id=?", [ids.entitlement]);
            for (const [table, key] of [
                ["marketplace_entitlements", "entitlement"], ["marketplace_order_items", "item"],
                ["marketplace_orders", "order"], ["marketplace_publications", "publication"],
                ["marketplace_product_versions", "version"], ["marketplace_offers", "offer"],
                ["marketplace_products", "product"], ["publishers", "publisher"], ["organizations", "organization"],
            ]) {
                await connection.execute(`DELETE FROM ${table} WHERE id=?`, [ids[key]]);
            }
        });
        if (fileCreated) {
            await unlink(file);
            fileCreated = false;
        }
    }
    try {
        await writeFile(file, bytes, { flag: "wx", mode: 0o600 });
        fileCreated = true;
        await platformTransaction(pool, async (connection) => {
            const [[category]] = await connection.execute("SELECT id FROM marketplace_categories WHERE active=TRUE ORDER BY id LIMIT 1");
            const [[license]] = await connection.execute("SELECT license_id,revision,acceptance_snapshot FROM marketplace_license_revisions ORDER BY license_id,revision LIMIT 1");
            assert.ok(category && license);
            await connection.execute(`INSERT INTO organizations (id,slug,display_name,created_by,created_at,updated_at)
                VALUES (?,?,'Private download QA',?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [ids.organization, `qa-${ids.organization}`, userId]);
            await connection.execute(`INSERT INTO publishers (id,organization_id,slug,display_name,summary,verified,created_at,updated_at)
                VALUES (?,?,?,'Private download QA','Disposable transport fixture',FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [ids.publisher, ids.organization, `qa-${ids.publisher}`]);
            await connection.execute(`INSERT INTO marketplace_products
                (id,publisher_id,category_id,slug,display_name,short_description,description_markdown,state,
                license_spdx,license_revision,featured,created_at,updated_at)
                VALUES (?,?,?,?,'Private download QA','Disposable private HTTPS download transport fixture',
                'Synthetic transport acceptance fixture. Not an installable asset package.','draft',?,?,FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [ids.product, ids.publisher, category.id, `qa-${ids.product}`, license.license_id, license.revision]);
            await connection.execute(`INSERT INTO marketplace_offers (id,product_id,currency,amount_minor,active,created_at)
                VALUES (?,?,'USD',0,FALSE,UTC_TIMESTAMP(6))`, [ids.offer, ids.product]);
            await connection.execute(`INSERT INTO marketplace_orders
                (id,user_id,currency,total_minor,status,idempotency_key,created_at)
                VALUES (?,?,'USD',0,'completed',?,UTC_TIMESTAMP(6))`, [ids.order, userId, ids.order]);
            await connection.execute(`INSERT INTO marketplace_order_items
                (id,order_id,product_id,offer_id,amount_minor,license_spdx,license_revision,accepted_license_snapshot)
                VALUES (?,?,?,?,0,?,?,?)`,
            [ids.item, ids.order, ids.product, ids.offer, license.license_id, license.revision, license.acceptance_snapshot]);
            await connection.execute(`INSERT INTO marketplace_entitlements (id,product_id,user_id,order_item_id,granted_at)
                VALUES (?,?,?,?,UTC_TIMESTAMP(6))`, [ids.entitlement, ids.product, userId, ids.item]);
            await connection.execute(`INSERT INTO marketplace_product_versions
                (id,product_id,version,state,install_kind,minimum_engine_version,platforms,architectures,renderer_capabilities,
                release_notes_markdown,archive_storage_path,archive_sha256,archive_size_bytes,manifest_sha256,signature_key_id,
                created_at,updated_at,archive_storage_bucket)
                VALUES (?,?,'1.0.0','withdrawn','asset_import','0.4.7','[]','[]','[]','Synthetic transport fixture',?,?,?,?,
                'qa-transport-only',UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),'marketplace-packages')`,
            [ids.version, ids.product, `qa/${ids.version}`, sha256, bytes.length, "b".repeat(64)]);
            await connection.execute(`INSERT INTO marketplace_publications
                (id,version_id,artifact_sha256,manifest_sha256,signature_key_id,signed_manifest,approved_by,published_at)
                VALUES (?,?,?,?,'qa-transport-only','synthetic-not-installable',?,UTC_TIMESTAMP(6))`,
            [ids.publication, ids.version, sha256, "b".repeat(64), userId]);
        });
        return { ...ids, bytes, sha256, close };
    } catch (error) {
        await close();
        throw error;
    }
}
