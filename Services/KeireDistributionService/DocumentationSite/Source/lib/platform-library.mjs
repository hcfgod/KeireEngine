import { mysqlUtcTimestamp } from "./platform-account-import.mjs";

const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

/** session must come from the local authentication service, never from request JSON. */
export async function localLibraryPage(pool, session, { organizationId = null, cursor = null } = {}) {
    if (!session || !uuidPattern.test(session.userId) || ![1, 2].includes(session.assurance)) {
        throw new Error("An authenticated local session is required.");
    }
    if (organizationId !== null && !uuidPattern.test(organizationId)) {
        throw new Error("Invalid library organization ID.");
    }
    const conditions = ["e.revoked_at IS NULL", "account.deleted_at IS NULL",
        "account.email_confirmed_at IS NOT NULL", "(account.banned_until IS NULL OR account.banned_until<=UTC_TIMESTAMP(6))"];
    const parameters = [session.userId];
    if (organizationId) {
        conditions.push(`e.organization_id=? AND EXISTS (
            SELECT 1 FROM organization_memberships member JOIN organizations organization ON organization.id=member.organization_id
            WHERE member.organization_id=e.organization_id AND member.user_id=account.id AND organization.deleted_at IS NULL)`);
        parameters.push(organizationId);
    } else {
        conditions.push("e.organization_id IS NULL AND e.user_id=account.id");
    }
    if (cursor) {
        if (!uuidPattern.test(cursor.id)) {
            throw new Error("Invalid library cursor ID.");
        }
        const timestamp = mysqlUtcTimestamp(cursor.grantedAt);
        if (!timestamp) {
            throw new Error("Invalid library cursor timestamp.");
        }
        conditions.push("(e.granted_at<? OR (e.granted_at=? AND e.id>?))");
        parameters.push(timestamp, timestamp, cursor.id);
    }
    const [rows] = await pool.execute(`SELECT e.id,e.product_id,e.user_id,e.organization_id,e.granted_at,
        product.slug,product.display_name,product.short_description,product.state,product.license_spdx,
        publisher.slug AS publisher_slug,publisher.display_name AS publisher_name
        FROM marketplace_entitlements e JOIN platform_accounts account ON account.id=?
        JOIN marketplace_products product ON product.id=e.product_id
        JOIN publishers publisher ON publisher.id=product.publisher_id
        WHERE ${conditions.join(" AND ")}
        ORDER BY e.granted_at DESC,e.id ASC LIMIT 25`, parameters);
    return rows.map((row) => ({
        id: row.id,
        product_id: row.product_id,
        user_id: row.user_id,
        organization_id: row.organization_id,
        granted_at: row.granted_at.replace(" ", "T") + "Z",
        marketplace_products: {
            id: row.product_id, slug: row.slug, display_name: row.display_name,
            short_description: row.short_description, state: row.state, license_spdx: row.license_spdx,
            publishers: { slug: row.publisher_slug, display_name: row.publisher_name },
        },
    }));
}
