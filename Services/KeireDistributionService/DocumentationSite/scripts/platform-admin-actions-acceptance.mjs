import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { applyLocalAdminAction } from "../Source/lib/platform-admin-actions.mjs";

/** Uses a dedicated disposable connection. Temporary tables shadow production tables only on this connection. */
export async function verifyAdminActions(pool) {
    const raw = await pool.getConnection();
    const tables = ["platform_accounts", "platform_staff_members", "organizations", "publishers",
        "publisher_applications", "marketplace_reports", "platform_feature_flags", "platform_audit_events"];
    const names = new RegExp(`\\b(${tables.join("|")})\\b`, "g");
    const connection = {
        execute: (sql, values) => raw.execute(sql.replace(names, (table) => `${table}_qa_admin`), values),
        beginTransaction: () => raw.beginTransaction(), commit: () => raw.commit(), rollback: () => raw.rollback(),
        destroy: () => raw.destroy(),
    };
    const actor = randomUUID();
    const target = randomUUID();
    const organization = randomUUID();
    const application = randomUUID();
    const report = randomUUID();
    let checks = 0;
    try {
        for (const table of tables) {
            await raw.execute(`CREATE TEMPORARY TABLE ${table}_qa_admin LIKE ${table}`);
        }
        for (const id of [actor, target]) {
            await connection.execute(`INSERT INTO platform_accounts (id,app_metadata,user_metadata,source_sealed)
                VALUES (?,'{}','{}',?)`, [id, Buffer.from("synthetic")]);
        }
        await connection.execute(`INSERT INTO platform_staff_members
            (user_id,role,active,appointed_at,updated_at) VALUES (?,'administrator',TRUE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [actor]);
        await connection.execute(`INSERT INTO organizations (id,slug,display_name,created_by,created_at,updated_at)
            VALUES (?,'admin-qa-org','QA organization',?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [organization, actor]);
        await connection.execute(`INSERT INTO publisher_applications
            (id,applicant_user_id,organization_id,public_name,statement,state,created_at,updated_at)
            VALUES (?,?,?,'QA publisher','Synthetic application for transaction testing.','submitted',UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [application, target, organization]);
        await connection.execute(`INSERT INTO marketplace_reports
            (id,reporter_user_id,product_id,reason,details,state,created_at)
            VALUES (?,?,?,'other','Synthetic report','open',UTC_TIMESTAMP(6))`, [report, target, randomUUID()]);
        await connection.execute(`INSERT INTO platform_feature_flags (\`key\`,enabled,description,updated_at)
            VALUES ('qa_admin_enabled',FALSE,'Synthetic gate',UTC_TIMESTAMP(6))`);
        const authentication = { withSessionTransaction: async (_token, operation) => {
            await connection.beginTransaction();
            try {
                const result = await operation(connection, { userId: actor, assurance: 2 });
                await connection.commit();
                return result;
            } catch (error) {
                await connection.rollback();
                throw error;
            }
        } };
        const apply = (input) => applyLocalAdminAction(authentication, "synthetic", input);
        const approved = await apply({ operation: "application.decide", applicationId: application,
            decision: "approved", decisionNote: "QA approval", publisherSlug: "admin-qa-publisher" });
        assert.equal(approved.application_state, "approved");
        const [[publisher]] = await connection.execute("SELECT verified FROM publishers WHERE id=?", [approved.publisher_id]);
        assert.equal(publisher.verified, 1);
        checks += 2;
        await assert.rejects(apply({ operation: "application.decide", applicationId: application,
            decision: "rejected", decisionNote: "Already approved" }), { code: "publisher.application_not_reviewable" });
        checks++;
        assert.equal((await apply({ operation: "report.decide", reportId: report, state: "triaged" })).report_state, "triaged");
        assert.equal((await apply({ operation: "report.decide", reportId: report, state: "resolved", resolutionNote: "QA resolved" })).report_state, "resolved");
        await assert.rejects(apply({ operation: "report.decide", reportId: report, state: "dismissed", resolutionNote: "Too late" }),
            { code: "staff.report_not_reviewable" });
        checks += 3;
        await assert.rejects(apply({ operation: "staff.set", targetUserId: actor, role: "moderator", active: true,
            reason: "Remove last administrator" }), { code: "staff.last_administrator_required" });
        checks++;
        assert.equal((await apply({ operation: "staff.set", targetUserId: target, role: "administrator", active: true,
            reason: "QA second administrator" })).active, true);
        assert.equal((await apply({ operation: "feature.set", key: "qa_admin_enabled", enabled: true,
            reason: "QA enable" })).enabled, true);
        assert.equal((await apply({ operation: "staff.set", targetUserId: target, role: "moderator", active: false,
            reason: "QA revoke" })).active, false);
        checks += 3;
        const failingAuthentication = { withSessionTransaction: async (_token, operation) => {
            await connection.beginTransaction();
            try {
                return await operation({ execute: (sql, values) => {
                    if (sql.includes("INSERT INTO platform_audit_events")) {
                        throw new Error("Synthetic audit failure");
                    }
                    return connection.execute(sql, values);
                } }, { userId: actor, assurance: 2 });
            } finally {
                await connection.rollback();
            }
        } };
        await assert.rejects(applyLocalAdminAction(failingAuthentication, "synthetic", {
            operation: "feature.set", key: "qa_admin_enabled", enabled: false, reason: "Must roll back",
        }), /Synthetic audit failure/);
        const [[unchanged]] = await connection.execute("SELECT enabled FROM platform_feature_flags WHERE `key`='qa_admin_enabled'");
        assert.equal(unchanged.enabled, 1);
        checks += 2;
        const [[audit]] = await connection.execute("SELECT COUNT(*) AS total FROM platform_audit_events");
        assert.equal(Number(audit.total), 6);
        checks++;
        return { checks, temporaryTablesOnly: true };
    } finally {
        // Destroying this connection disposes every temporary fixture, including partial setup.
        connection.destroy();
    }
}
