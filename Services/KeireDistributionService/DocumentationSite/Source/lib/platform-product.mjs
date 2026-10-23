import { localCatalogPage } from "./platform-catalog.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

function publicTimestamp(value) {
    return value === null ? null : value.replace(" ", "T") + "Z";
}

/** Resolve public URLs without trusting slugs as SQL or as ownership evidence. */
export async function localProductPage(pool, publisherSlug, productSlug, session) {
    const validSlug = (value) => typeof value === "string" && /^[a-z0-9][a-z0-9-]{1,95}$/.test(value);
    if (!validSlug(publisherSlug) || !validSlug(productSlug)) {
        return { enabled: true, product: null, ownsPersonalCopy: false };
    }
    const [matches] = await pool.execute(`SELECT p.id FROM marketplace_products p
        JOIN publishers publisher ON publisher.id=p.publisher_id
        WHERE publisher.slug=? AND p.slug=?`, [publisherSlug, productSlug]);
    if (!matches.length) {
        return { enabled: true, product: null, ownsPersonalCopy: false };
    }
    const detail = await localProductDetail(pool, matches[0].id);
    let ownsPersonalCopy = false;
    if (detail.product && session) {
        const [entitlements] = await pool.execute(`SELECT id FROM marketplace_entitlements
            WHERE product_id=? AND user_id=? AND organization_id IS NULL AND revoked_at IS NULL LIMIT 1`,
        [detail.product.id, session.userId]);
        ownsPersonalCopy = entitlements.length === 1;
    }
    return { ...detail, ownsPersonalCopy };
}

/** Read visibility and public child records from one consistent database snapshot. */
export async function localProductDetail(pool, productId) {
    return platformTransaction(pool, async (connection) => {
        const catalog = await localCatalogPage(connection, { limit: 1, productId });
        if (!catalog.enabled || !catalog.values.length) {
            return { enabled: catalog.enabled, product: null };
        }

        const [versions] = await connection.execute(`SELECT id,version,state,install_kind,
            minimum_engine_version,maximum_engine_version,platforms,architectures,
            renderer_capabilities,managed_api_version,release_notes_markdown,published_at,
            (archive_storage_bucket='marketplace-packages' AND archive_storage_path IS NOT NULL)
                AS download_available
            FROM marketplace_product_versions WHERE product_id=?
            AND state IN ('published','withdrawn','security_revoked')
            ORDER BY published_at DESC,id`, [productId]);
        const [media] = await connection.execute(`SELECT id,storage_path,media_type,alt_text,
            width,height,sort_order,sha256 FROM marketplace_product_media
            WHERE product_id=? AND approved_at IS NOT NULL ORDER BY sort_order,id`, [productId]);
        const [reviews] = await connection.execute(`SELECT id,rating,title,body,publisher_reply,created_at,updated_at
            FROM marketplace_reviews WHERE product_id=? AND hidden_at IS NULL
            ORDER BY created_at DESC,id LIMIT 50`, [productId]);
        const [offers] = await connection.execute(`SELECT amount_minor,currency FROM marketplace_offers
            WHERE product_id=? AND active=TRUE`, [productId]);
        if (offers.length > 1) {
            throw new Error("Ambiguous active product offer.");
        }
        const offer = offers.length ? { amount_minor: Number(offers[0].amount_minor), currency: offers[0].currency } : null;
        if (offer && (!Number.isSafeInteger(offer.amount_minor) || offer.amount_minor < 0)) {
            throw new Error("Invalid active product price.");
        }

        return {
            enabled: true,
            product: {
                ...catalog.values[0],
                versions: versions.map((version) => ({ ...version,
                    download_available: Boolean(version.download_available),
                    published_at: publicTimestamp(version.published_at),
                })),
                media,
                reviews: reviews.map((review) => ({
                    ...review,
                    created_at: publicTimestamp(review.created_at),
                    updated_at: publicTimestamp(review.updated_at),
                })),
                offer,
            },
        };
    });
}
