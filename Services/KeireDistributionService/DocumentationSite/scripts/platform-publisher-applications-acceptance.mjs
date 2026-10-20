import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { createLocalOrganization } from "../Source/lib/platform-organizations.mjs";
import { saveLocalPublisherApplication } from "../Source/lib/platform-publisher-applications.mjs";
import { localPublisherPortal } from "../Source/lib/platform-publisher-portal.mjs";
import { selectUploadProduct } from "../Source/lib/platform-upload-product.mjs";
import { localUploadInput } from "../Source/lib/platform-upload-input.mjs";
import { reserveLocalUpload } from "../Source/lib/platform-upload-reservation.mjs";

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
        const portal = await localPublisherPortal(auth(), "fixture");
        assert.equal(portal.enabled, true);
        assert.deepEqual(portal.applications.map((item) => item.id), [applicationId]);
        assert.deepEqual(portal.organizations.map((item) => item.organizations.id), [organization.organizationId]);
        for (const field of ["publishers", "products", "versions", "uploads", "validationReports"]) {
            assert.deepEqual(portal[field], []);
        }
        const outsider = await localPublisherPortal(auth(2, randomUUID()), "fixture");
        assert.deepEqual(outsider.applications, []);
        assert.deepEqual(outsider.organizations, []);
        await assert.rejects(localPublisherPortal(auth(1), "fixture"), { code: "account.mfa_required" });
        checks += 11;
        const [existingPublishers] = await connection.execute(`SELECT publisher.id,publisher.organization_id
            FROM publishers publisher JOIN organizations organization ON organization.id=publisher.organization_id
            WHERE publisher.suspended_at IS NULL AND organization.deleted_at IS NULL
            AND EXISTS (SELECT 1 FROM marketplace_products product WHERE product.publisher_id=publisher.id)
            ORDER BY publisher.id LIMIT 1`);
        assert.equal(existingPublishers.length, 1);
        const existingPublisher = existingPublishers[0];
        // Only a synthetic user's membership is added, and the enclosing transaction always rolls back.
        await connection.execute(`INSERT INTO organization_memberships
            (organization_id,user_id,role,invited_by,created_at) VALUES (?,?,'admin',?,UTC_TIMESTAMP(6))`,
        [existingPublisher.organization_id, userId, userId]);
        const authorized = await localPublisherPortal(auth(), "fixture");
        assert.deepEqual(authorized.publishers.map((item) => item.id), [existingPublisher.id]);
        assert.ok(authorized.products.length > 0);
        assert.ok(authorized.products.every((item) => item.publisher_id === existingPublisher.id));
        const [expectedVersions] = await connection.execute(`SELECT version.id FROM marketplace_product_versions version
            JOIN marketplace_products product ON product.id=version.product_id WHERE product.publisher_id=?
            ORDER BY version.updated_at DESC,version.id LIMIT 100`, [existingPublisher.id]);
        assert.deepEqual(authorized.versions.map((item) => item.id), expectedVersions.map((item) => item.id));
        await connection.execute("UPDATE organization_memberships SET role='member' WHERE organization_id=? AND user_id=?",
            [existingPublisher.organization_id, userId]);
        const downgraded = await localPublisherPortal(auth(), "fixture");
        for (const field of ["publishers", "products", "versions", "uploads", "validationReports"]) {
            assert.deepEqual(downgraded[field], []);
        }
        checks += 10;
        const publisherId = randomUUID();
        await connection.execute(`INSERT INTO publishers
            (id,organization_id,slug,display_name,summary,verified,created_at,updated_at)
            VALUES (?,?,?,'QA upload publisher','',FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [publisherId, organization.organizationId, `qa-${publisherId}`]);
        const [categories] = await connection.execute("SELECT id FROM marketplace_categories WHERE active=TRUE ORDER BY id LIMIT 1");
        const [licenses] = await connection.execute("SELECT license_id,revision FROM marketplace_license_revisions ORDER BY license_id,revision LIMIT 1");
        const uploadInput = localUploadInput({
            publisherId, categoryId: categories[0].id, productName: "QA Upload Product",
            productSummary: "A disposable product for upload reservation validation.",
            licenseSpdx: licenses[0].license_id, licenseRevision: licenses[0].revision,
            version: "1.0.0", minimumEngineVersion: "0.4.7", installKind: "asset_import",
            platforms: ["windows"], architectures: ["x64"], rendererCapabilities: [],
            expectedSizeBytes: 100, expectedSha256: "a".repeat(64),
        });
        const actor = { userId, assurance: 2 };
        await assert.rejects(selectUploadProduct(connection, { userId, assurance: 1 }, uploadInput),
            { code: "account.mfa_required" });
        await assert.rejects(selectUploadProduct(connection, { userId: randomUUID(), assurance: 2 }, uploadInput),
            { code: "publisher.not_manageable" });
        const productId = await selectUploadProduct(connection, actor, uploadInput);
        assert.equal(await selectUploadProduct(connection, actor, uploadInput), productId);
        const [offers] = await connection.execute("SELECT amount_minor,active FROM marketplace_offers WHERE product_id=?", [productId]);
        assert.equal(offers.length, 1);
        assert.equal(Number(offers[0].amount_minor), 0);
        assert.equal(offers[0].active, 1);
        const collisionId = await selectUploadProduct(connection, actor, { ...uploadInput, productName: "QA Upload Product!" });
        assert.notEqual(collisionId, productId);
        await connection.execute("UPDATE marketplace_products SET state='published' WHERE id=?", [productId]);
        assert.equal(await selectUploadProduct(connection, actor, { ...uploadInput, productId }), productId);
        await assert.rejects(selectUploadProduct(connection, actor,
            { ...uploadInput, productId, productSummary: "Changed summary must not overwrite a published listing." }),
        { code: "publisher.published_product_metadata_immutable" });
        await assert.rejects(selectUploadProduct(connection, actor, { ...uploadInput, productId: randomUUID() }),
            { code: "publisher.product_not_editable" });
        checks += 10;
        const reserved = await reserveLocalUpload(auth(), "fixture", { ...uploadInput, productId });
        assert.equal(reserved.productId, productId);
        assert.match(reserved.storagePath, new RegExp(`/${"a".repeat(64)}\\.keireassetpackage$`));
        assert.ok(Date.parse(reserved.expiresAt) > Date.now());
        await assert.rejects(reserveLocalUpload(auth(), "fixture", { ...uploadInput, productId }),
            { code: "publisher.upload_already_active" });
        await connection.execute("UPDATE marketplace_uploads SET expires_at='2000-01-01' WHERE id=?", [reserved.uploadId]);
        const replacement = await reserveLocalUpload(auth(), "fixture",
            { ...uploadInput, productId, expectedSha256: "b".repeat(64) });
        assert.equal(replacement.versionId, reserved.versionId);
        assert.notEqual(replacement.uploadId, reserved.uploadId);
        const [oldUpload] = await connection.execute("SELECT state FROM marketplace_uploads WHERE id=?", [reserved.uploadId]);
        assert.equal(oldUpload[0].state, "expired");
        await connection.execute("UPDATE marketplace_product_versions SET state='published' WHERE id=?", [reserved.versionId]);
        await assert.rejects(reserveLocalUpload(auth(), "fixture", { ...uploadInput, productId }),
            { code: "publisher.version_not_editable" });
        checks += 8;
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
