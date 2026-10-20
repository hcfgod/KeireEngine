import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

/** Isolated publisher fixture; requests use the HTTPS suite's real authenticated browser cookie. */
export async function verifyUploadHttp(pool, userId, organizationId, request, progress = () => {}) {
    const publisherId = randomUUID();
    try {
        await pool.execute(`INSERT INTO publishers
            (id,organization_id,slug,display_name,summary,verified,created_at,updated_at)
            VALUES (?,?,?,'HTTP upload QA','',FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [publisherId, organizationId, `qa-${publisherId}`]);
        const [categories] = await pool.execute("SELECT id FROM marketplace_categories WHERE active=TRUE ORDER BY id LIMIT 1");
        const [licenses] = await pool.execute(
            "SELECT license_id,revision FROM marketplace_license_revisions ORDER BY license_id,revision LIMIT 1");
        const bytes = Buffer.from("Synthetic package for real HTTPS resumable transfer.");
        const input = {
            publisherId, categoryId: categories[0].id, productName: "HTTP upload QA",
            productSummary: "Disposable package for HTTPS upload acceptance.",
            licenseSpdx: licenses[0].license_id, licenseRevision: licenses[0].revision,
            version: "1.0.0", minimumEngineVersion: "0.4.7", installKind: "asset_import",
            platforms: ["windows"], architectures: ["x64"], rendererCapabilities: [],
            expectedSizeBytes: bytes.length, expectedSha256: createHash("sha256").update(bytes).digest("hex"),
        };
        progress("reserve");
        const reserved = await request("/publisher/v1/uploads/", input);
        assert.equal(reserved.status, 201);
        const upload = (await reserved.json()).data;
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
        progress("retry");
        const retryResponse = await request("/publisher/v1/uploads/", input);
        assert.equal(retryResponse.status, 201);
        const retry = (await retryResponse.json()).data;
        assert.notEqual(retry.uploadId, upload.uploadId);
        const transferHeaders = { "tus-resumable": "1.0.0", "x-signature": retry.uploadId,
            "content-type": "application/offset+octet-stream" };
        progress("first-chunk");
        const first = await request(retry.uploadUrl, bytes.subarray(0, 10),
            { ...transferHeaders, "upload-offset": "0" }, "PATCH");
        assert.equal(first.status, 204, (await first.text()).slice(0, 200));
        assert.equal(first.headers.get("upload-offset"), "10");
        progress("resume");
        const resume = await request(`/publisher/v1/uploads/${retry.uploadId}/prepare/`, {});
        assert.equal(resume.status, 200);
        assert.equal((await resume.json()).data.offset, 10);
        const head = await request(retry.uploadUrl, undefined, transferHeaders, "HEAD");
        assert.equal(head.status, 200);
        assert.equal(head.headers.get("upload-offset"), "10");
        progress("incomplete");
        assert.equal((await request(`/publisher/v1/uploads/${retry.uploadId}/complete/`, {})).status, 409);
        const rest = await request(retry.uploadUrl, bytes.subarray(10),
            { ...transferHeaders, "upload-offset": "10" }, "PATCH");
        assert.equal(rest.status, 204);
        progress("complete");
        const complete = await request(`/publisher/v1/uploads/${retry.uploadId}/complete/`, {});
        assert.equal(complete.status, 200);
        assert.equal((await complete.json()).data.state, "uploaded");
        const finalStatus = await request(`/publisher/v1/uploads/${retry.uploadId}/`);
        assert.equal((await finalStatus.json()).data.versionState, "uploaded");
        return 28;
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
