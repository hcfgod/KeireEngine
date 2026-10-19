import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { localCatalogPage } from "../Source/lib/platform-catalog.mjs";
import { marketplaceTables } from "../Source/lib/platform-marketplace-schema.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

export async function verifyLocalCatalog(pool) {
    const initial = await localCatalogPage(pool, { limit: 50 });
    let checks = 0;
    const rollback = new Error("rollback catalog fixtures");
    await assert.rejects(platformTransaction(pool, async (connection) => {
        await connection.execute("UPDATE platform_feature_flags SET enabled=TRUE WHERE `key`='marketplace_enabled'");
        const table = marketplaceTables().find((entry) => entry.name === "marketplace_products");
        const names = Object.keys(table.columns);
        const [products] = await connection.query(`SELECT ${names.map((name) => `\`${name}\``).join(",")}
            FROM marketplace_products ORDER BY id LIMIT 1`);
        assert.equal(products.length, 1);
        const fixture = { ...products[0], id: randomUUID(), slug: `qa-${randomUUID()}`,
            display_name: `QA 100%_ ${randomUUID()}`, state: "published", featured: 1,
            published_at: "2026-10-19 12:34:56.123456" };
        await connection.execute(`INSERT INTO marketplace_products (${names.map((name) => `\`${name}\``).join(",")})
            VALUES (${names.map(() => "?").join(",")})`, names.map((name) => fixture[name]));
        const findFixture = async () => (await localCatalogPage(connection, { search: fixture.display_name })).values;
        assert.equal((await findFixture()).length, 1);
        checks++;
        assert.equal((await localCatalogPage(connection, { search: fixture.display_name.toLowerCase() })).values.length, 1);
        checks++;
        const row = (await findFixture())[0];
        assert.equal(row.published_at, "2026-10-19T12:34:56.123456Z");
        assert.equal((await localCatalogPage(connection, { search: fixture.display_name,
            cursor: { featured: row.featured, publishedAt: row.published_at, id: row.id } })).values.length, 0);
        checks += 2;
        for (const state of ["draft", "delisted", "suspended"]) {
            await connection.execute("UPDATE marketplace_products SET state=? WHERE id=?", [state, fixture.id]);
            assert.equal((await findFixture()).length, 0);
            checks++;
        }
        await connection.execute("UPDATE marketplace_products SET state='published' WHERE id=?", [fixture.id]);
        await connection.execute("UPDATE publishers SET suspended_at=UTC_TIMESTAMP(6) WHERE id=?", [fixture.publisher_id]);
        assert.equal((await findFixture()).length, 0);
        checks++;
        await connection.execute("UPDATE publishers SET suspended_at=NULL WHERE id=?", [fixture.publisher_id]);
        await connection.execute("UPDATE marketplace_categories SET active=FALSE WHERE id=?", [fixture.category_id]);
        assert.equal((await findFixture()).length, 0);
        checks++;
        await connection.execute("UPDATE platform_feature_flags SET enabled=FALSE WHERE `key`='marketplace_enabled'");
        assert.equal((await localCatalogPage(connection)).enabled, false);
        checks++;
        throw rollback;
    }), (error) => error === rollback);
    assert.deepEqual(await localCatalogPage(pool, { limit: 50 }), initial);
    return checks + 1;
}
