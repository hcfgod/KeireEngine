import { mysqlUtcTimestamp } from "./platform-account-import.mjs";

export async function localMarketplaceListing(pool, search = "", category = "") {
    const empty = { products: [], categories: [] };
    try {
        const page = await localCatalogPage(pool, { limit: 30, search, category });
        if (!page.enabled) {
            return { ...empty, status: "disabled" };
        }
        const [categories] = await pool.execute(`SELECT slug,display_name FROM marketplace_categories
            WHERE active=TRUE ORDER BY sort_order,slug`);
        return { status: "ready", products: page.values.slice(0, 30), categories };
    } catch {
        return { ...empty, status: "unavailable" };
    }
}

export async function localCatalogPage(pool, { limit = 24, search = "", category = "", cursor = null } = {}) {
    if (!Number.isInteger(limit) || limit < 1 || limit > 50 || typeof search !== "string" || search.length > 100 ||
        typeof category !== "string" || category.length > 64) {
        throw new Error("Invalid catalog query.");
    }
    const [flags] = await pool.execute("SELECT enabled FROM platform_feature_flags WHERE `key`='marketplace_enabled'");
    if (flags.length !== 1 || !flags[0].enabled) {
        return { enabled: false, values: [] };
    }
    const conditions = ["p.state='published'", "publisher.suspended_at IS NULL", "category.active=TRUE"];
    const parameters = [];
    if (search) {
        conditions.push("LOWER(p.display_name) LIKE LOWER(?) ESCAPE '!'");
        parameters.push(`%${search.replaceAll("!", "!!").replaceAll("%", "!%").replaceAll("_", "!_")}%`);
    }
    if (category) {
        conditions.push("category.slug=?");
        parameters.push(category);
    }
    if (cursor) {
        if (typeof cursor.featured !== "boolean" || !/^[0-9a-f-]{36}$/.test(cursor.id)) {
            throw new Error("Invalid catalog cursor.");
        }
        const timestamp = mysqlUtcTimestamp(cursor.publishedAt);
        if (!timestamp) {
            throw new Error("Invalid catalog cursor timestamp.");
        }
        conditions.push(`(p.featured < ? OR (p.featured=? AND p.published_at < ?) OR
            (p.featured=? AND p.published_at=? AND p.id > ?))`);
        parameters.push(Number(cursor.featured), Number(cursor.featured), timestamp,
            Number(cursor.featured), timestamp, cursor.id);
    }
    const [values] = await pool.execute(`SELECT p.id,p.slug,p.display_name,p.short_description,p.description_markdown,
        p.license_spdx,p.license_revision,license.acceptance_snapshot AS license_acceptance_snapshot,
        p.featured,p.published_at,publisher.id AS publisher_id,publisher.slug AS publisher_slug,
        publisher.display_name AS publisher_name,publisher.verified AS publisher_verified,
        category.slug AS category_slug,category.display_name AS category_name,
        COALESCE((SELECT ROUND(AVG(r.rating),2) FROM marketplace_reviews r
            WHERE r.product_id=p.id AND r.hidden_at IS NULL),0) AS rating_average,
        (SELECT COUNT(*) FROM marketplace_reviews r WHERE r.product_id=p.id AND r.hidden_at IS NULL) AS rating_count
        FROM marketplace_products p JOIN publishers publisher ON publisher.id=p.publisher_id
        JOIN marketplace_categories category ON category.id=p.category_id
        JOIN marketplace_license_revisions license ON license.license_id=p.license_spdx AND license.revision=p.license_revision
        WHERE ${conditions.join(" AND ")}
        ORDER BY p.featured DESC,p.published_at DESC,p.id ASC LIMIT ${limit + 1}`, parameters);
    return { enabled: true, values: values.map((row) => ({ ...row,
        featured: Boolean(row.featured), publisher_verified: Boolean(row.publisher_verified),
        rating_average: Number(row.rating_average), rating_count: Number(row.rating_count),
        published_at: row.published_at === null ? null : row.published_at.replace(" ", "T") + "Z",
    })) };
}
