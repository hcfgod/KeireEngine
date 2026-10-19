import { randomUUID } from "node:crypto";

const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

export class LocalClaimError extends Error {
    constructor(status, code, message) {
        super(message);
        this.status = status;
        this.code = code;
    }
}

function reject(status, code, message) {
    throw new LocalClaimError(status, `marketplace.${code}`, message);
}

/** The submitted license is compared with the locked revision, never replaced silently. */
export async function claimLocalProduct(auth, sessionToken, input) {
    const { productId, organizationId = null, idempotencyKey, licenseSpdx,
        licenseRevision, acceptedLicenseSnapshot } = input;
    if (!uuidPattern.test(productId ?? "") ||
        (organizationId !== null && !uuidPattern.test(organizationId)) ||
        typeof idempotencyKey !== "string" || idempotencyKey.length < 16 || idempotencyKey.length > 128 ||
        typeof licenseSpdx !== "string" || licenseSpdx.length < 2 || licenseSpdx.length > 64 ||
        typeof licenseRevision !== "string" || licenseRevision.length < 1 || licenseRevision.length > 64 ||
        typeof acceptedLicenseSnapshot !== "string" || acceptedLicenseSnapshot.length < 1 ||
        acceptedLicenseSnapshot.length > 100000) {
        reject(400, "invalid_claim", "The asset claim or license acceptance is invalid.");
    }
    const result = await auth.withSessionTransaction(sessionToken, async (connection, session) => {
        // The account lock serializes this actor's idempotency keys. Organization locks also serialize shared claims.
        if (organizationId !== null) {
            const [organizations] = await connection.execute(
                "SELECT id FROM organizations WHERE id=? AND deleted_at IS NULL FOR UPDATE", [organizationId]);
            const [members] = await connection.execute(
                "SELECT role FROM organization_memberships WHERE organization_id=? AND user_id=? FOR UPDATE",
                [organizationId, session.userId]);
            if (organizations.length !== 1 || !["owner", "admin"].includes(members[0]?.role)) {
                reject(403, "organization_forbidden", "An organization owner or administrator must acquire this asset.");
            }
        }
        const [flags] = await connection.execute(
            "SELECT enabled FROM platform_feature_flags WHERE `key`='marketplace_enabled' FOR SHARE");
        if (!flags[0]?.enabled) {
            reject(503, "unavailable", "The marketplace is temporarily unavailable.");
        }
        const [products] = await connection.execute(`SELECT p.id,p.license_spdx,p.license_revision,
            license.acceptance_snapshot FROM marketplace_products p
            JOIN publishers publisher ON publisher.id=p.publisher_id
            JOIN marketplace_categories category ON category.id=p.category_id
            JOIN marketplace_license_revisions license ON license.license_id=p.license_spdx AND license.revision=p.license_revision
            WHERE p.id=? AND p.state='published' AND publisher.suspended_at IS NULL AND category.active=TRUE FOR SHARE`, [productId]);
        const product = products[0];
        if (!product) {
            reject(404, "product_not_found", "This asset is no longer available.");
        }
        if (product.license_spdx !== licenseSpdx || product.license_revision !== licenseRevision ||
            product.acceptance_snapshot !== acceptedLicenseSnapshot) {
            reject(409, "license_revision_changed", "The asset license changed. Reload the page and review it before continuing.");
        }
        const [offers] = await connection.execute(
            "SELECT id,currency,amount_minor FROM marketplace_offers WHERE product_id=? AND active=TRUE FOR SHARE", [productId]);
        if (offers.length !== 1) {
            reject(409, "offer_unavailable", "This asset does not currently have an available offer.");
        }
        const offer = offers[0];
        if (String(offer.amount_minor) !== "0") {
            reject(409, "purchase_unavailable", "This asset requires a purchase. Checkout is not available yet.");
        }
        const [orders] = await connection.execute(
            "SELECT id,organization_id,status FROM marketplace_orders WHERE user_id=? AND idempotency_key=? FOR UPDATE",
            [session.userId, idempotencyKey]);
        if (orders.length) {
            const order = orders[0];
            const [items] = await connection.execute(
                "SELECT product_id,license_spdx,license_revision,accepted_license_snapshot FROM marketplace_order_items WHERE order_id=?",
                [order.id]);
            const item = items[0];
            if (order.organization_id !== organizationId || order.status !== "completed" || items.length !== 1 ||
                item.product_id !== productId || item.license_spdx !== licenseSpdx ||
                item.license_revision !== licenseRevision || item.accepted_license_snapshot !== acceptedLicenseSnapshot) {
                reject(409, "idempotency_key_reused", "This request identifier was already used for a different acquisition.");
            }
        }
        const [entitlements] = await connection.execute(`SELECT id,revoked_at FROM marketplace_entitlements
            WHERE product_id=? AND ${organizationId === null ? "user_id=? AND organization_id IS NULL" : "organization_id=?"}
            FOR UPDATE`, [productId, organizationId ?? session.userId]);
        if (entitlements.length) {
            if (entitlements[0].revoked_at !== null) {
                reject(403, "entitlement_revoked", "Access to this asset was revoked. Contact support for assistance.");
            }
            return { entitlementId: entitlements[0].id, organizationId };
        }
        if (orders.length) {
            reject(409, "acquisition_inconsistent", "This acquisition needs support assistance before it can be completed.");
        }
        const orderId = randomUUID();
        const itemId = randomUUID();
        const entitlementId = randomUUID();
        await connection.execute(`INSERT INTO marketplace_orders
            (id,user_id,organization_id,currency,total_minor,status,idempotency_key,created_at)
            VALUES (?,?,?,?,0,'completed',?,UTC_TIMESTAMP(6))`,
        [orderId, session.userId, organizationId, offer.currency, idempotencyKey]);
        await connection.execute(`INSERT INTO marketplace_order_items
            (id,order_id,product_id,offer_id,amount_minor,license_spdx,license_revision,accepted_license_snapshot)
            VALUES (?,?,?,?,0,?,?,?)`, [itemId, orderId, productId, offer.id, licenseSpdx, licenseRevision, acceptedLicenseSnapshot]);
        await connection.execute(`INSERT INTO marketplace_entitlements
            (id,product_id,user_id,organization_id,order_item_id,granted_at)
            VALUES (?,?,?,?,?,UTC_TIMESTAMP(6))`,
        [entitlementId, productId, organizationId === null ? session.userId : null, organizationId, itemId]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,'marketplace.product_claimed','marketplace_entitlement',?,?,?)`,
        [session.userId, entitlementId, randomUUID(), JSON.stringify({ productId, organizationId })]);
        return { entitlementId, organizationId };
    });
    if (!result) {
        reject(401, "authentication_required", "Sign in again to acquire this asset.");
    }
    return result;
}
