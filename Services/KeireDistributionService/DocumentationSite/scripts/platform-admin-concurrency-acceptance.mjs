import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { applyLocalAdminAction } from "../Source/lib/platform-admin-actions.mjs";

/** Real independent transactions; the staff scan is scoped to new synthetic IDs, never existing administrators. */
export async function verifyAdminConcurrency(pool) {
    const actors = [randomUUID(), randomUUID()];
    try {
        for (const actor of actors) {
            await pool.execute(`INSERT INTO platform_accounts (id,email,email_key,app_metadata,user_metadata,source_sealed)
                VALUES (?,?,?,'{}','{}',?)`,
            [actor, `admin-concurrency-${actor}@invalid.example`, `admin-concurrency-${actor}@invalid.example`, Buffer.from("synthetic")]);
            await pool.execute(`INSERT INTO platform_staff_members
                (user_id,role,active,appointed_at,updated_at) VALUES (?,'administrator',TRUE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [actor]);
        }
        let arrived = 0;
        let start;
        const ready = new Promise((resolve) => { start = resolve; });
        const authentication = (actor) => ({ withSessionTransaction: async (_token, operation) => {
            const raw = await pool.getConnection();
            try {
                await raw.beginTransaction();
                await raw.execute("SELECT id FROM platform_accounts WHERE id=? FOR UPDATE", [actor]);
                arrived++;
                if (arrived === 2) {
                    start();
                }
                let timeout;
                try {
                    await Promise.race([ready, new Promise((_, reject) => {
                        timeout = setTimeout(() => reject(new Error("Concurrent QA participants did not become ready.")), 10000);
                    })]);
                } finally {
                    clearTimeout(timeout);
                }
                const connection = { execute: (sql, values) => {
                    const staffScan = "FROM platform_staff_members ORDER BY user_id FOR UPDATE";
                    if (sql.includes(staffScan)) {
                        return raw.execute(sql.replace(staffScan,
                            "FROM platform_staff_members WHERE user_id IN (?,?) ORDER BY user_id FOR UPDATE"), actors);
                    }
                    return raw.execute(sql, values);
                } };
                const result = await operation(connection, { userId: actor, assurance: 2 });
                await raw.commit();
                return result;
            } catch (error) {
                await raw.rollback();
                throw error;
            } finally {
                raw.release();
            }
        } });
        const outcomes = await Promise.allSettled(actors.map((actor) => applyLocalAdminAction(authentication(actor), "synthetic", {
            operation: "staff.set", targetUserId: actor, role: "moderator", active: true, reason: "Concurrent isolated QA demotion",
        })));
        assert.equal(outcomes.filter((outcome) => outcome.status === "fulfilled").length, 1);
        const denied = outcomes.find((outcome) => outcome.status === "rejected");
        assert.equal(denied.reason.code, "staff.last_administrator_required");
        const [[remaining]] = await pool.execute(`SELECT COUNT(*) AS total FROM platform_staff_members
            WHERE user_id IN (?,?) AND role='administrator' AND active=TRUE AND revoked_at IS NULL`, actors);
        assert.equal(Number(remaining.total), 1);
        const [[audits]] = await pool.execute("SELECT COUNT(*) AS total FROM platform_audit_events WHERE actor_user_id IN (?,?)", actors);
        assert.equal(Number(audits.total), 1);
        return { checks: 4, independentTransactions: true, staffScanScopedToSyntheticAccounts: true };
    } finally {
        const connection = await pool.getConnection();
        try {
            await connection.beginTransaction();
            await connection.execute("DELETE FROM platform_audit_events WHERE actor_user_id IN (?,?)", actors);
            await connection.execute("DELETE FROM platform_staff_members WHERE user_id IN (?,?)", actors);
            await connection.execute("DELETE FROM platform_accounts WHERE id IN (?,?) AND email_key LIKE 'admin-concurrency-%@invalid.example'", actors);
            await connection.commit();
        } catch (error) {
            await connection.rollback();
            throw error;
        } finally {
            connection.release();
        }
    }
}
