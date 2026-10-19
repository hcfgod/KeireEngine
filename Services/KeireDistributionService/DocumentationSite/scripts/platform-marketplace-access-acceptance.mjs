import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { verifyMarketplaceRecords } from "../Source/lib/platform-marketplace-import.mjs";

export async function verifyAccessConstraints(pool, records) {
    const rollback = new Error("intentional access acceptance rollback");
    let checks = 0;
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const reject = async (sql, values, code) => {
            await assert.rejects(connection.execute(sql, values), (error) => error.code === code);
            checks++;
        };
        await reject("UPDATE marketplace_entitlements SET user_id = NULL, organization_id = NULL LIMIT 1", [],
            "ER_CHECK_CONSTRAINT_VIOLATED");
        const [owners] = await connection.query("SELECT id FROM organizations LIMIT 1");
        const [users] = await connection.query("SELECT id FROM platform_accounts LIMIT 1");
        assert.equal(owners.length, 1);
        assert.equal(users.length, 1);
        await reject("UPDATE marketplace_entitlements SET user_id = ?, organization_id = ? LIMIT 1", [users[0].id, owners[0].id],
            "ER_CHECK_CONSTRAINT_VIOLATED");
        const entitlements = records.find((record) => record.table.name === "marketplace_entitlements");
        assert.ok(entitlements.values.length > 0);
        const duplicate = [...entitlements.values[0]];
        duplicate[entitlements.names.indexOf("id")] = randomUUID();
        await reject(`INSERT INTO marketplace_entitlements (${entitlements.names.map((name) => `\`${name}\``).join(",")})
            VALUES (${duplicate.map(() => "?").join(",")})`, duplicate, "ER_DUP_ENTRY");
        await reject(`INSERT INTO marketplace_orders (id,user_id,organization_id,currency,total_minor,status,idempotency_key,created_at)
            SELECT ?,user_id,organization_id,currency,total_minor,status,idempotency_key,created_at
            FROM marketplace_orders LIMIT 1`, [randomUUID()], "ER_DUP_ENTRY");
        await reject("UPDATE marketplace_orders SET total_minor = 1 LIMIT 1", [], "ER_CHECK_CONSTRAINT_VIOLATED");

        const deviceId = randomUUID();
        await connection.execute(`INSERT INTO oauth_device_sessions
            (id,user_id,oauth_session_id,client_type,device_name,created_at,last_used_at,revoked_at)
            VALUES (?,?,?,'hub','Isolated migration QA',UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),NULL)`,
        [deviceId, users[0].id, randomUUID()]);
        const [references] = await connection.query(`SELECT e.id AS entitlement_id,v.id AS version_id
            FROM marketplace_entitlements e JOIN marketplace_product_versions v ON v.product_id = e.product_id LIMIT 1`);
        assert.equal(references.length, 1);
        const grantId = randomUUID();
        const grantValues = [grantId, references[0].entitlement_id, references[0].version_id, deviceId];
        await reject(`INSERT INTO marketplace_download_grants
            (id,entitlement_id,version_id,session_id,created_at,expires_at,used_at)
            VALUES (?,?,?,?,UTC_TIMESTAMP(6),TIMESTAMPADD(MINUTE,16,UTC_TIMESTAMP(6)),NULL)`,
        grantValues, "ER_CHECK_CONSTRAINT_VIOLATED");
        await connection.execute(`INSERT INTO marketplace_download_grants
            (id,entitlement_id,version_id,session_id,created_at,expires_at,used_at)
            VALUES (?,?,?,?,UTC_TIMESTAMP(6),TIMESTAMPADD(MINUTE,10,UTC_TIMESTAMP(6)),NULL)`, grantValues);
        await connection.execute("DELETE FROM oauth_device_sessions WHERE id = ?", [deviceId]);
        const [remaining] = await connection.execute("SELECT COUNT(*) AS count FROM marketplace_download_grants WHERE id = ?", [grantId]);
        assert.equal(Number(remaining[0].count), 0);
        checks++;
        await connection.query("UPDATE marketplace_orders SET status = 'cancelled' WHERE status = 'completed' LIMIT 1");
        throw rollback;
    }), (error) => error === rollback);
    await verifyMarketplaceRecords(pool, records);
    return checks + 1;
}
