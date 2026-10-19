import { localCatalogPage } from "./platform-catalog.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

export async function localPublisherPage(pool, slug, cursor = null) {
    if (typeof slug !== "string" || !/^[a-z0-9][a-z0-9-]{1,95}$/.test(slug)) {
        return { enabled: true, publisher: null, products: [], hasMore: false };
    }
    return platformTransaction(pool, async (connection) => {
        const [flags] = await connection.execute("SELECT enabled FROM platform_feature_flags WHERE `key`='marketplace_enabled'");
        if (flags.length !== 1 || !flags[0].enabled) {
            return { enabled: false, publisher: null, products: [], hasMore: false };
        }
        const [publishers] = await connection.execute(`SELECT id,slug,display_name,summary,website_url,verified,created_at
            FROM publishers WHERE slug=? AND suspended_at IS NULL`, [slug]);
        if (publishers.length !== 1) {
            return { enabled: true, publisher: null, products: [], hasMore: false };
        }
        const publisher = publishers[0];
        const page = await localCatalogPage(connection, { publisherId: publisher.id, limit: 30, cursor });
        return {
            enabled: true,
            publisher: { ...publisher, verified: Boolean(publisher.verified),
                created_at: publisher.created_at.replace(" ", "T") + "Z" },
            products: page.values.slice(0, 30),
            hasMore: page.values.length > 30,
        };
    });
}
