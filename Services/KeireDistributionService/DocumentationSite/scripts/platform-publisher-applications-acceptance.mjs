import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { createLocalOrganization } from "../Source/lib/platform-organizations.mjs";
import { saveLocalPublisherApplication } from "../Source/lib/platform-publisher-applications.mjs";

export async function verifyPublisherApplications(pool, userId) {
    const rollback = new Error("Rollback publisher application fixtures");
    const slug = `qa-${randomUUID()}`;
    let applicationId;
    let checks = 0;
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const auth = (assurance = 2, actor = userId) => ({
            withSessionTransaction: async (_token, action) => action(connection, { userId: actor, assurance }),
        });
        const organization = await createLocalOrganization(auth(), "fixture", { slug, displayName: "Publisher QA" });
        const input = { organizationId: organization.organizationId, publicName: "QA publisher",
            statement: "A synthetic application used to verify publisher authorization.", websiteUrl: "https://example.com" };
        await assert.rejects(saveLocalPublisherApplication(auth(1), "fixture", input), { code: "account.mfa_required" });
        const saved = await saveLocalPublisherApplication(auth(), "fixture", input);
        applicationId = saved.applicationId;
        assert.equal(saved.state, "draft");
        const [drafts] = await connection.execute(
            "SELECT website_url,submitted_at FROM publisher_applications WHERE id=?", [applicationId]);
        assert.equal(drafts[0].website_url, "https://example.com/");
        assert.equal(drafts[0].submitted_at, null);
        await assert.rejects(saveLocalPublisherApplication(auth(2, randomUUID()), "fixture",
            { ...input, applicationId }), { code: "publisher.application_not_editable" });
        await connection.execute("UPDATE organization_memberships SET role='member' WHERE organization_id=? AND user_id=?",
            [organization.organizationId, userId]);
        await assert.rejects(saveLocalPublisherApplication(auth(), "fixture",
            { ...input, applicationId }), { code: "organization.authorization_required" });
        await connection.execute("UPDATE organization_memberships SET role='admin' WHERE organization_id=? AND user_id=?",
            [organization.organizationId, userId]);
        await connection.execute("UPDATE organizations SET deleted_at=UTC_TIMESTAMP(6) WHERE id=?", [organization.organizationId]);
        await assert.rejects(saveLocalPublisherApplication(auth(), "fixture",
            { ...input, applicationId }), { code: "organization.authorization_required" });
        await connection.execute("UPDATE organizations SET deleted_at=NULL WHERE id=?", [organization.organizationId]);
        await connection.execute("UPDATE publisher_applications SET state='changes_requested',decision_note='More details' WHERE id=?",
            [applicationId]);
        await saveLocalPublisherApplication(auth(), "fixture", { ...input, applicationId });
        const [notes] = await connection.execute("SELECT decision_note FROM publisher_applications WHERE id=?", [applicationId]);
        assert.equal(notes[0].decision_note, "More details");
        const submitted = await saveLocalPublisherApplication(auth(), "fixture", { ...input, applicationId, action: "submit" });
        assert.equal(submitted.state, "submitted");
        const [rows] = await connection.execute(
            "SELECT state,submitted_at,decision_note FROM publisher_applications WHERE id=?", [applicationId]);
        assert.equal(rows[0].state, "submitted");
        assert.ok(rows[0].submitted_at);
        assert.equal(rows[0].decision_note, null);
        await assert.rejects(saveLocalPublisherApplication(auth(), "fixture",
            { ...input, applicationId }), { code: "publisher.application_not_editable" });
        const [audit] = await connection.execute(
            "SELECT actor_user_id FROM platform_audit_events WHERE target_id=? AND action='publisher.application_submitted'",
            [applicationId]);
        assert.deepEqual(audit, [{ actor_user_id: userId }]);
        checks += 14;
        throw rollback;
    }), (error) => error === rollback);
    const [remaining] = await pool.execute("SELECT id FROM publisher_applications WHERE id=?", [applicationId]);
    assert.equal(remaining.length, 0);
    const [organizations] = await pool.execute("SELECT id FROM organizations WHERE slug=?", [slug]);
    assert.equal(organizations.length, 0);
    const failedSlug = `qa-${randomUUID()}`;
    const auditFailure = new Error("Synthetic publisher audit failure");
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const authentication = {
            withSessionTransaction: async (_token, action) => action(connection, { userId, assurance: 2 }),
        };
        const organization = await createLocalOrganization(authentication, "fixture",
            { slug: failedSlug, displayName: "Publisher rollback QA" });
        const guardedConnection = {
            execute: (sql, parameters) => {
                if (sql.includes("INSERT INTO platform_audit_events")) {
                    throw auditFailure;
                }
                return connection.execute(sql, parameters);
            },
        };
        await saveLocalPublisherApplication({
            withSessionTransaction: async (_token, action) => action(guardedConnection, { userId, assurance: 2 }),
        }, "fixture", { organizationId: organization.organizationId, publicName: failedSlug,
            statement: "Synthetic publisher audit failure must roll back all writes.", action: "submit" });
    }), (error) => error === auditFailure);
    const [failedApplications] = await pool.execute("SELECT id FROM publisher_applications WHERE public_name=?", [failedSlug]);
    assert.equal(failedApplications.length, 0);
    const [failedOrganizations] = await pool.execute("SELECT id FROM organizations WHERE slug=?", [failedSlug]);
    assert.equal(failedOrganizations.length, 0);
    return checks + 5;
}
