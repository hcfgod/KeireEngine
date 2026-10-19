import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { localAccountOverview } from "../Source/lib/platform-account-view.mjs";
import { sealAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { localLibraryPage } from "../Source/lib/platform-library.mjs";

/** All fixtures and mutations roll back. Existing products/orders are read only. */
export async function verifyAccountVisibility(pool, userId, encryptionKey) {
    const otherUserId = randomUUID();
    const organizationId = randomUUID();
    const personalId = randomUUID();
    const sharedId = randomUUID();
    let checks = 0;
    const rollback = new Error("rollback account visibility fixtures");
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const [items] = await connection.query("SELECT id,product_id FROM marketplace_order_items ORDER BY id LIMIT 1");
        assert.equal(items.length, 1);
        const item = items[0];
        const email = `visibility-${otherUserId}@invalid.example`;
        await connection.execute(`INSERT INTO platform_accounts
            (id,email,email_key,email_confirmed_at,app_metadata,user_metadata,source_sealed)
            VALUES (?,?,?,'2000-01-01','{}','{}',?)`,
        [otherUserId, email, email, sealAccountRecord({ fixture: true }, encryptionKey, `account:${otherUserId}`)]);
        await connection.execute(`INSERT INTO organizations
            (id,slug,display_name,created_by,created_at,updated_at)
            VALUES (?,?,'QA visibility organization',?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [organizationId, `qa-${organizationId}`, userId]);
        const visible = async (accountId, expected) => {
            const overview = await localAccountOverview(connection, { userId: accountId, assurance: 2 });
            assert.deepEqual(overview.entitlements.map((row) => row.id).sort(), [...expected].sort());
            checks++;
            const personal = await localLibraryPage(connection, { userId: accountId, assurance: 2 });
            const shared = await localLibraryPage(connection, { userId: accountId, assurance: 2 }, { organizationId });
            assert.deepEqual([...personal, ...shared].map((row) => row.id).sort(), [...expected].sort());
            assert.ok(personal.every((row) => row.user_id === accountId && row.organization_id === null));
            assert.ok(shared.every((row) => row.organization_id === organizationId));
            checks += 3;
            return overview;
        };
        await visible(userId, []);
        await visible(otherUserId, []);
        await connection.execute(`INSERT INTO marketplace_entitlements
            (id,product_id,user_id,order_item_id,granted_at) VALUES (?,?,?,?,UTC_TIMESTAMP(6))`,
        [personalId, item.product_id, userId, item.id]);
        await connection.execute(`INSERT INTO marketplace_entitlements
            (id,product_id,organization_id,order_item_id,granted_at) VALUES (?,?,?,?,UTC_TIMESTAMP(6))`,
        [sharedId, item.product_id, organizationId, item.id]);
        await visible(userId, [personalId]);
        await visible(otherUserId, []);
        for (const memberId of [userId, otherUserId]) {
            await connection.execute(`INSERT INTO organization_memberships
                (organization_id,user_id,role,created_at) VALUES (?,?,'member',UTC_TIMESTAMP(6))`,
            [organizationId, memberId]);
        }
        await visible(userId, [personalId, sharedId]);
        await visible(otherUserId, [sharedId]);
        await connection.execute("DELETE FROM organization_memberships WHERE organization_id=? AND user_id=?",
            [organizationId, otherUserId]);
        await visible(otherUserId, []);
        await connection.execute("UPDATE organizations SET deleted_at=UTC_TIMESTAMP(6) WHERE id=?", [organizationId]);
        const hidden = await visible(userId, [personalId]);
        assert.equal(hidden.organizations.length, 0);
        checks++;
        await connection.execute("UPDATE organizations SET deleted_at=NULL WHERE id=?", [organizationId]);
        await connection.execute("UPDATE marketplace_entitlements SET revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [sharedId]);
        await visible(userId, [personalId]);
        await connection.execute("UPDATE marketplace_entitlements SET revoked_at=UTC_TIMESTAMP(6) WHERE id=?", [personalId]);
        await visible(userId, []);
        throw rollback;
    }), (error) => error === rollback);
    const overview = await localAccountOverview(pool, { userId, assurance: 2 });
    assert.equal(overview.entitlements.length, 0);
    const [otherAccounts] = await pool.execute("SELECT id FROM platform_accounts WHERE id=?", [otherUserId]);
    assert.equal(otherAccounts.length, 0);
    return checks + 2;
}
