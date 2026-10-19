import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { claimLocalProduct } from "../Source/lib/platform-claims.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { localCatalogPage } from "../Source/lib/platform-catalog.mjs";

/** Exercise actual claim writes inside a transaction deliberately rolled back with the synthetic account. */
export async function verifyLocalClaims(pool, userId) {
    const rollback = new Error("rollback claim fixtures");
    let checks = 0;
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const product = (await localCatalogPage(connection, { limit: 1 })).values[0];
        assert.ok(product);
        const auth = { withSessionTransaction: async (_token, action) => action(connection, { userId, assurance: 2 }) };
        const input = {
            productId: product.id,
            idempotencyKey: randomUUID(),
            licenseSpdx: product.license_spdx,
            licenseRevision: product.license_revision,
            acceptedLicenseSnapshot: product.license_acceptance_snapshot,
        };
        const claim = (changes = {}) => claimLocalProduct(auth, "fixture", { ...input, ...changes });
        await assert.rejects(claim({ acceptedLicenseSnapshot: "not accepted" }), { code: "marketplace.license_revision_changed" });
        await assert.rejects(claim({ productId: "invalid" }), { code: "marketplace.invalid_claim" });
        await assert.rejects(claim({ organizationId: randomUUID() }), { code: "marketplace.organization_forbidden" });
        checks += 3;
        const acquired = await claim();
        assert.deepEqual(await claim(), acquired);
        assert.deepEqual(await claim({ idempotencyKey: randomUUID() }), acquired);
        const [orders] = await connection.execute("SELECT id FROM marketplace_orders WHERE user_id=?", [userId]);
        const [events] = await connection.execute(
            "SELECT id FROM platform_audit_events WHERE actor_user_id=? AND action='marketplace.product_claimed'", [userId]);
        assert.equal(orders.length, 1);
        assert.equal(events.length, 1);
        checks += 4;
        await connection.execute("UPDATE marketplace_entitlements SET revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [acquired.entitlementId]);
        await assert.rejects(claim(), { code: "marketplace.entitlement_revoked" });
        checks++;
        const organizationId = randomUUID();
        await connection.execute(`INSERT INTO organizations (id,slug,display_name,created_by,created_at,updated_at)
            VALUES (?,?,'QA claims',?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [organizationId, `qa-${organizationId}`, userId]);
        await connection.execute(`INSERT INTO organization_memberships (organization_id,user_id,role,created_at)
            VALUES (?,?,'member',UTC_TIMESTAMP(6))`, [organizationId, userId]);
        await assert.rejects(claim({ organizationId }), { code: "marketplace.organization_forbidden" });
        await connection.execute("UPDATE organization_memberships SET role='owner' WHERE organization_id=? AND user_id=?", [organizationId, userId]);
        await assert.rejects(claim({ organizationId }), { code: "marketplace.idempotency_key_reused" });
        const organizationClaim = await claim({ organizationId, idempotencyKey: randomUUID() });
        assert.equal(organizationClaim.organizationId, organizationId);
        assert.notEqual(organizationClaim.entitlementId, acquired.entitlementId);
        checks += 4;
        throw rollback;
    }), (error) => error === rollback);
    const [remaining] = await pool.execute("SELECT id FROM marketplace_orders WHERE user_id=?", [userId]);
    assert.equal(remaining.length, 0);
    return checks + 1;
}
