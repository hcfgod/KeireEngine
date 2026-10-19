import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { verifyMarketplaceRecords } from "../Source/lib/platform-marketplace-import.mjs";
import { marketplaceCoreTables } from "../Source/lib/platform-marketplace-schema.mjs";

export async function verifyContentConstraints(pool, records) {
    const rollback = new Error("intentional content acceptance rollback");
    let checks = 0;
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const reject = async (sql, values, codes) => {
            await assert.rejects(connection.execute(sql, values), (error) => codes.includes(error.code));
            checks++;
        };
        const productId = randomUUID();
        const product = marketplaceCoreTables.find((table) => table.name === "marketplace_products");
        const names = Object.keys(product.columns);
        const select = names.map((name) => ["id", "slug"].includes(name) ? "?" : `\`${name}\``);
        const [inserted] = await connection.execute(`INSERT INTO marketplace_products (${names.map((name) => `\`${name}\``).join(",")})
            SELECT ${select.join(",")} FROM marketplace_products LIMIT 1`, [productId, `qa-${productId}`]);
        assert.equal(inserted.affectedRows, 1);
        const mediaId = randomUUID();
        await connection.execute(`INSERT INTO marketplace_product_media
            (id,product_id,storage_path,media_type,alt_text,width,height,sort_order,approved_at,sha256)
            VALUES (?,?,'qa/valid.png','image/png','Isolated migration fixture',64,64,0,NULL,?)`,
        [mediaId, productId, "a".repeat(64)]);
        for (const [field, value] of [["storage_path", "qa/../private.png"], ["sha256", "invalid"], ["width", 0], ["media_type", "text/html"]]) {
            await reject(`UPDATE marketplace_product_media SET ${field} = ? WHERE id = ?`, [value, mediaId],
                ["ER_CHECK_CONSTRAINT_VIOLATED"]);
        }
        await reject("UPDATE marketplace_product_media SET product_id = ? WHERE id = ?", [randomUUID(), mediaId],
            ["ER_NO_REFERENCED_ROW", "ER_NO_REFERENCED_ROW_2"]);

        const tagId = randomUUID();
        await connection.execute("INSERT INTO marketplace_tags (id,slug,display_name) VALUES (?,?,'Migration fixture')",
            [tagId, `qa-${tagId}`]);
        await connection.execute("INSERT INTO marketplace_product_tags (product_id,tag_id) VALUES (?,?)", [productId, tagId]);
        await connection.execute("DELETE FROM marketplace_tags WHERE id = ?", [tagId]);
        const [tags] = await connection.execute("SELECT COUNT(*) AS count FROM marketplace_product_tags WHERE tag_id = ?", [tagId]);
        assert.equal(Number(tags[0].count), 0);
        checks++;

        const [versions] = await connection.query("SELECT id FROM marketplace_product_versions LIMIT 1");
        assert.equal(versions.length, 1);
        const packageId = `qa.${randomUUID()}`;
        await connection.execute(`INSERT INTO marketplace_version_dependencies (version_id,package_id,version_range,conflict)
            VALUES (?,?,'>=0.4.7',0),(?,?,'<0.4.7',1)`, [versions[0].id, packageId, versions[0].id, packageId]);
        checks++;
        await reject(`INSERT INTO marketplace_version_dependencies (version_id,package_id,version_range,conflict)
            VALUES (?,?,'*',0)`, [versions[0].id, packageId], ["ER_DUP_ENTRY"]);
        await reject(`INSERT INTO marketplace_version_dependencies (version_id,package_id,version_range,conflict)
            VALUES (?,'invalid','*',0)`, [versions[0].id], ["ER_CHECK_CONSTRAINT_VIOLATED"]);

        const [users] = await connection.query("SELECT id FROM platform_accounts LIMIT 1");
        assert.equal(users.length, 1);
        await connection.execute("INSERT INTO marketplace_wishlist_items (user_id,product_id,created_at) VALUES (?,?,UTC_TIMESTAMP(6))",
            [users[0].id, productId]);
        await reject("INSERT INTO marketplace_wishlist_items (user_id,product_id,created_at) VALUES (?,?,UTC_TIMESTAMP(6))",
            [users[0].id, productId], ["ER_DUP_ENTRY"]);
        await connection.execute("DELETE FROM marketplace_products WHERE id = ?", [productId]);
        for (const table of ["marketplace_product_media", "marketplace_wishlist_items"]) {
            const [remaining] = await connection.execute(`SELECT COUNT(*) AS count FROM ${table} WHERE product_id = ?`, [productId]);
            assert.equal(Number(remaining[0].count), 0);
            checks++;
        }
        throw rollback;
    }), (error) => error === rollback);
    await verifyMarketplaceRecords(pool, records);
    return checks + 1;
}
