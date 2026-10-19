import assert from "node:assert/strict";
import { requestLocalAccountData } from "../Source/lib/platform-data-requests.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

export async function verifyLocalDataRequests(pool, userId) {
    const rollback = new Error("rollback data request fixture");
    let checks = 0;
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const auth = { withSessionTransaction: async (_token, action) => action(connection, { userId, assurance: 1 }) };
        await assert.rejects(requestLocalAccountData(auth, "fixture", "export"), { code: "account.profile_unavailable" });
        await connection.execute(`INSERT INTO profiles (user_id,created_at,updated_at,locale,notification_preferences)
            VALUES (?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),'en','{}')`, [userId]);
        await assert.rejects(requestLocalAccountData(auth, "fixture", "invalid"), { code: "account.invalid_data_request" });
        for (const kind of ["export", "deletion"]) {
            assert.deepEqual(await requestLocalAccountData(auth, "fixture", kind), { kind, requested: true });
            assert.deepEqual(await requestLocalAccountData(auth, "fixture", kind), { kind, requested: true });
        }
        const [profiles] = await connection.execute("SELECT export_requested_at,deletion_requested_at FROM profiles WHERE user_id=?", [userId]);
        assert.ok(profiles[0].export_requested_at);
        assert.ok(profiles[0].deletion_requested_at);
        const [events] = await connection.execute(`SELECT action FROM platform_audit_events
            WHERE actor_user_id=? AND action IN ('account.export_requested','account.deletion_requested') ORDER BY action`, [userId]);
        assert.deepEqual(events.map((event) => event.action), ["account.deletion_requested", "account.export_requested"]);
        const [accounts] = await connection.execute("SELECT deleted_at FROM platform_accounts WHERE id=?", [userId]);
        assert.equal(accounts[0].deleted_at, null);
        checks += 10;
        throw rollback;
    }), (error) => error === rollback);
    const [profiles] = await pool.execute("SELECT user_id FROM profiles WHERE user_id=?", [userId]);
    assert.equal(profiles.length, 0);
    return checks + 1;
}
