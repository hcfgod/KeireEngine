import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { reserveLocalUpload } from "../Source/lib/platform-upload-reservation.mjs";

/** Isolated publisher fixture; requests use the HTTPS suite's real authenticated browser cookie. */
export async function verifyUploadHttp(pool, userId, organizationId, request) {
    const publisherId = randomUUID();
    const authentication = {
        withSessionTransaction: (_token, action) => platformTransaction(pool,
            (connection) => action(connection, { userId, assurance: 2 })),
    };
    try {
        await pool.execute(`INSERT INTO publishers
            (id,organization_id,slug,display_name,summary,verified,created_at,updated_at)
            VALUES (?,?,?,'HTTP upload QA','',FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [publisherId, organizationId, `qa-${publisherId}`]);
        const [categories] = await pool.execute("SELECT id FROM marketplace_categories WHERE active=TRUE ORDER BY id LIMIT 1");
        const [licenses] = await pool.execute(
            "SELECT license_id,revision FROM marketplace_license_revisions ORDER BY license_id,revision LIMIT 1");
        const upload = await reserveLocalUpload(authentication, "fixture", {
            publisherId, categoryId: categories[0].id, productName: "HTTP upload QA",
            productSummary: "Disposable package for HTTPS upload acceptance.",
            licenseSpdx: licenses[0].license_id, licenseRevision: licenses[0].revision,
            version: "1.0.0", minimumEngineVersion: "0.4.7", installKind: "asset_import",
            platforms: ["windows"], architectures: ["x64"], rendererCapabilities: [],
            expectedSizeBytes: 100, expectedSha256: "a".repeat(64),
        });
        const route = `/publisher/v1/uploads/${upload.uploadId}/`;
        const status = await request(route);
        assert.equal(status.status, 200);
        assert.equal(status.headers.get("cache-control"), "private, no-store");
        assert.deepEqual((await status.json()).data, {
            uploadId: upload.uploadId, state: "created", validationAttempts: 0,
            versionId: upload.versionId, versionState: "draft", validation: null,
        });
        assert.equal((await request(route, undefined, { cookie: "" })).status, 401);
        assert.equal((await request(route, undefined, { authorization: "Bearer unsupported" })).status, 401);
        assert.equal((await request(`/publisher/v1/uploads/${randomUUID()}/`)).status, 404);
        assert.equal((await request("/publisher/v1/uploads/not-an-id/")).status, 400);
        assert.equal((await request(route, undefined, { origin: "https://other.invalid" }, "DELETE")).status, 403);
        await pool.execute("UPDATE organization_memberships SET role='member' WHERE organization_id=? AND user_id=?",
            [organizationId, userId]);
        try {
            assert.equal((await request(route)).status, 404);
            assert.equal((await request(route, undefined, {}, "DELETE")).status, 404);
        } finally {
            await pool.execute("UPDATE organization_memberships SET role='owner' WHERE organization_id=? AND user_id=?",
                [organizationId, userId]);
        }
        const cancelled = await request(route, undefined, {}, "DELETE");
        assert.equal(cancelled.status, 200);
        assert.deepEqual((await cancelled.json()).data, { uploadId: upload.uploadId, state: "expired" });
        assert.equal((await request(route, undefined, {}, "DELETE")).status, 200);
        assert.equal((await (await request(route)).json()).data.state, "expired");
        return 14;
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
        });
    }
}
