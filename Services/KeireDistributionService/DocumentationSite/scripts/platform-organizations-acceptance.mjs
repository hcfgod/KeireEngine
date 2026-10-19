import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { createLocalOrganization } from "../Source/lib/platform-organizations.mjs";

/** Synthetic organization writes are rolled back, including the audit record. */
export async function verifyLocalOrganizations(pool, userId) {
    const rollback = new Error("rollback organization fixtures");
    const slug = `qa-${randomUUID()}`;
    let checks = 0;
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const auth = (assurance) => ({
            withSessionTransaction: async (_token, action) => action(connection, { userId, assurance }),
        });
        const input = { slug, displayName: "QA organization" };
        await assert.rejects(createLocalOrganization(auth(1), "fixture", input), { code: "account.mfa_required" });
        await assert.rejects(createLocalOrganization(auth(2), "fixture", { ...input, displayName: "   " }),
            { code: "organization.invalid_input" });
        const created = await createLocalOrganization(auth(2), "fixture", { ...input, slug: slug.toUpperCase() });
        assert.equal(created.slug, slug);
        const [members] = await connection.execute(
            "SELECT user_id,role,invited_by FROM organization_memberships WHERE organization_id=?", [created.organizationId]);
        assert.deepEqual(members, [{ user_id: userId, role: "owner", invited_by: userId }]);
        const [audit] = await connection.execute(
            "SELECT actor_user_id FROM platform_audit_events WHERE target_id=? AND action='organization.created'",
            [created.organizationId]);
        assert.deepEqual(audit, [{ actor_user_id: userId }]);
        await assert.rejects(createLocalOrganization(auth(2), "fixture", input), { code: "organization.slug_unavailable" });
        checks += 6;
        throw rollback;
    }), (error) => error === rollback);
    const [remaining] = await pool.execute("SELECT id FROM organizations WHERE slug=?", [slug]);
    assert.equal(remaining.length, 0);
    checks++;

    const failedSlug = `qa-${randomUUID()}`;
    const auditFailure = new Error("synthetic audit failure");
    const failingAuthentication = {
        withSessionTransaction: (_token, action) => platformTransaction(pool, async (connection) => {
            const guardedConnection = {
                execute: (sql, parameters) => {
                    if (sql.includes("INSERT INTO platform_audit_events")) {
                        throw auditFailure;
                    }
                    return connection.execute(sql, parameters);
                },
            };
            return action(guardedConnection, { userId, assurance: 2 });
        }),
    };
    await assert.rejects(createLocalOrganization(failingAuthentication, "fixture",
        { slug: failedSlug, displayName: "QA rollback" }), (error) => error === auditFailure);
    const [failedOrganizations] = await pool.execute("SELECT id FROM organizations WHERE slug=?", [failedSlug]);
    assert.equal(failedOrganizations.length, 0);
    const [failedMemberships] = await pool.execute("SELECT organization_id FROM organization_memberships WHERE user_id=?", [userId]);
    assert.equal(failedMemberships.length, 0);
    return checks + 3;
}
