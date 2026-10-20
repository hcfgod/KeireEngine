import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { reserveLocalUpload } from "../Source/lib/platform-upload-reservation.mjs";

export async function verifyUploadConcurrency(pool, userId) {
    const organizationId = randomUUID();
    const publisherId = randomUUID();
    const actor = { userId, assurance: 2 };
    const authentication = {
        withSessionTransaction: (_token, action) => platformTransaction(pool,
            (connection) => action(connection, actor)),
    };
    let checks = 0;
    try {
        await platformTransaction(pool, async (connection) => {
            await connection.execute(`INSERT INTO organizations
                (id,slug,display_name,created_by,created_at,updated_at)
                VALUES (?,?,'Concurrent upload QA',?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [organizationId, `qa-${organizationId}`, userId]);
            await connection.execute(`INSERT INTO organization_memberships
                (organization_id,user_id,role,invited_by,created_at) VALUES (?,?,'owner',?,UTC_TIMESTAMP(6))`,
            [organizationId, userId, userId]);
            await connection.execute(`INSERT INTO publishers
                (id,organization_id,slug,display_name,summary,verified,created_at,updated_at)
                VALUES (?,?,?,'Concurrent upload QA','',FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [publisherId, organizationId, `qa-${publisherId}`]);
        });
        const [categories] = await pool.execute("SELECT id FROM marketplace_categories WHERE active=TRUE ORDER BY id LIMIT 1");
        const [licenses] = await pool.execute("SELECT license_id,revision FROM marketplace_license_revisions ORDER BY license_id,revision LIMIT 1");
        const input = { publisherId, categoryId: categories[0].id, productName: "Concurrent QA package",
            productSummary: "A disposable package for concurrent reservation tests.",
            licenseSpdx: licenses[0].license_id, licenseRevision: licenses[0].revision,
            version: "1.0.0", minimumEngineVersion: "0.4.7", installKind: "asset_import",
            platforms: ["windows"], architectures: ["x64"], rendererCapabilities: [],
            expectedSizeBytes: 100, expectedSha256: "c".repeat(64) };
        const outcomes = await Promise.allSettled([
            reserveLocalUpload(authentication, "fixture", input),
            reserveLocalUpload(authentication, "fixture", input),
        ]);
        assert.equal(outcomes.filter((item) => item.status === "fulfilled").length, 1);
        const failure = outcomes.find((item) => item.status === "rejected");
        assert.equal(failure.reason.code, "publisher.upload_already_active");
        const reservation = outcomes.find((item) => item.status === "fulfilled").value;
        const [products] = await pool.execute("SELECT id FROM marketplace_products WHERE publisher_id=?", [publisherId]);
        assert.equal(products.length, 1);
        const [uploads] = await pool.execute("SELECT id FROM marketplace_uploads WHERE version_id=?", [reservation.versionId]);
        assert.deepEqual(uploads.map((item) => item.id), [reservation.uploadId]);
        await assert.rejects(reserveLocalUpload(authentication, "fixture",
            { ...input, productId: reservation.productId, productSummary: "This changed summary must roll back on conflict." }),
        { code: "publisher.upload_already_active" });
        const [unchanged] = await pool.execute("SELECT short_description FROM marketplace_products WHERE id=?", [reservation.productId]);
        assert.equal(unchanged[0].short_description, input.productSummary);
        checks += 6;

        const auditFailure = new Error("Synthetic upload audit failure");
        const failingAuthentication = {
            withSessionTransaction: (_token, action) => platformTransaction(pool, (connection) => action({
                execute: (sql, parameters) => {
                    if (parameters?.includes("marketplace.upload_reserved") ||
                        sql.includes("'marketplace.upload_reserved'")) {
                        throw auditFailure;
                    }
                    return connection.execute(sql, parameters);
                },
            }, actor)),
        };
        await assert.rejects(reserveLocalUpload(failingAuthentication, "fixture",
            { ...input, productName: "Rollback QA package" }), (error) => error === auditFailure);
        const [afterFailure] = await pool.execute("SELECT id FROM marketplace_products WHERE publisher_id=?", [publisherId]);
        assert.deepEqual(afterFailure.map((item) => item.id), [reservation.productId]);
        checks += 2;
    } finally {
        await platformTransaction(pool, async (connection) => {
            await connection.execute(`DELETE audit FROM platform_audit_events audit
                JOIN marketplace_uploads upload ON upload.id=audit.target_id
                JOIN marketplace_product_versions version ON version.id=upload.version_id
                JOIN marketplace_products product ON product.id=version.product_id
                WHERE product.publisher_id=? AND audit.actor_user_id=?`, [publisherId, userId]);
            await connection.execute(`DELETE audit FROM platform_audit_events audit
                JOIN marketplace_products product ON product.id=audit.target_id
                WHERE product.publisher_id=? AND audit.actor_user_id=?`, [publisherId, userId]);
            await connection.execute(`DELETE version FROM marketplace_product_versions version
                JOIN marketplace_products product ON product.id=version.product_id WHERE product.publisher_id=?`, [publisherId]);
            await connection.execute("DELETE FROM marketplace_products WHERE publisher_id=?", [publisherId]);
            await connection.execute("DELETE FROM publishers WHERE id=?", [publisherId]);
            await connection.execute("DELETE FROM organizations WHERE id=?", [organizationId]);
        });
    }
    const [remaining] = await pool.execute("SELECT id FROM publishers WHERE id=?", [publisherId]);
    assert.equal(remaining.length, 0);
    return checks + 1;
}
