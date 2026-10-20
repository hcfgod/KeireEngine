import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { mkdtemp, readdir, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { LocalBlobStore } from "../Source/lib/local-blob-store.ts";
import { transferLocalUpload } from "../Source/lib/platform-upload-transfer.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { reserveLocalUpload } from "../Source/lib/platform-upload-reservation.mjs";
import { localUploadStatus } from "../Source/lib/platform-upload-status.mjs";
import { cancelLocalUpload } from "../Source/lib/platform-upload-cancel.mjs";

export async function verifyUploadConcurrency(pool, userId) {
    const organizationId = randomUUID();
    const publisherId = randomUUID();
    const actor = { userId, assurance: 2 };
    const authentication = {
        withSessionTransaction: (_token, action) => platformTransaction(pool,
            (connection) => action(connection, actor)),
    };
    let checks = 0;
    const temporaryRoot = await mkdtemp(path.join(tmpdir(), "keire-upload-transfer-"));
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
        const bytes = Buffer.from("Synthetic private package bytes for local transfer acceptance.");
        const identity = { sha256: createHash("sha256").update(bytes).digest("hex"), sizeBytes: bytes.length };
        const transferReservation = await reserveLocalUpload(authentication, "fixture", {
            ...input, productId: reservation.productId, version: "1.0.1",
            expectedSizeBytes: identity.sizeBytes, expectedSha256: identity.sha256,
        });
        const store = new LocalBlobStore(temporaryRoot);
        async function* chunks(value) {
            yield value.subarray(0, 10);
            yield value.subarray(10);
        }
        await assert.rejects(transferLocalUpload(authentication, "fixture", transferReservation.uploadId,
            store, chunks(Buffer.alloc(bytes.length))), /SHA-256/);
        assert.deepEqual(await readdir(temporaryRoot), []);
        const [failedTransfer] = await pool.execute("SELECT state,uploaded_at FROM marketplace_uploads WHERE id=?",
            [transferReservation.uploadId]);
        assert.equal(failedTransfer[0].state, "uploading");
        assert.equal(failedTransfer[0].uploaded_at, null);
        const disconnected = new Error("Synthetic client disconnect");
        async function* interruptedChunks() {
            yield bytes.subarray(0, 10);
            throw disconnected;
        }
        await assert.rejects(transferLocalUpload(authentication, "fixture", transferReservation.uploadId,
            store, interruptedChunks()), (error) => error === disconnected);
        assert.deepEqual(await readdir(temporaryRoot), []);
        const completionFailure = new Error("Synthetic completion audit failure");
        const completionAuthentication = {
            withSessionTransaction: (_token, action) => platformTransaction(pool, (connection) => action({
                execute: (sql, parameters) => {
                    if (sql.includes("'marketplace.upload_completed'")) {
                        throw completionFailure;
                    }
                    return connection.execute(sql, parameters);
                },
            }, actor)),
        };
        await assert.rejects(transferLocalUpload(completionAuthentication, "fixture", transferReservation.uploadId,
            store, chunks(bytes)), (error) => error === completionFailure);
        await store.verify(identity);
        const [rolledBackCompletion] = await pool.execute(`SELECT upload.state,version.state AS version_state
            FROM marketplace_uploads upload JOIN marketplace_product_versions version ON version.id=upload.version_id
            WHERE upload.id=?`, [transferReservation.uploadId]);
        assert.equal(rolledBackCompletion[0].state, "uploading");
        assert.equal(rolledBackCompletion[0].version_state, "draft");
        checks += 5;
        const completed = await transferLocalUpload(authentication, "fixture", transferReservation.uploadId,
            store, chunks(bytes));
        assert.equal(completed.state, "uploaded");
        await store.verify(identity);
        const stored = [];
        for await (const chunk of store.read(identity)) {
            stored.push(chunk);
        }
        assert.deepEqual(Buffer.concat(stored), bytes);
        const [completedRows] = await pool.execute(`SELECT upload.state,upload.uploaded_at,version.state AS version_state
            FROM marketplace_uploads upload JOIN marketplace_product_versions version ON version.id=upload.version_id
            WHERE upload.id=?`, [transferReservation.uploadId]);
        assert.equal(completedRows[0].state, "uploaded");
        assert.equal(completedRows[0].version_state, "uploaded");
        assert.ok(completedRows[0].uploaded_at);
        const [completedAudit] = await pool.execute(
            "SELECT metadata FROM platform_audit_events WHERE target_id=? AND action='marketplace.upload_completed'",
            [transferReservation.uploadId]);
        assert.equal(completedAudit.length, 1);
        assert.equal(completedAudit[0].metadata.sha256, identity.sha256);
        assert.deepEqual(await localUploadStatus(authentication, "fixture", transferReservation.uploadId), {
            uploadId: transferReservation.uploadId,
            state: "uploaded",
            validationAttempts: 0,
            versionId: transferReservation.versionId,
            versionState: "uploaded",
            validation: null,
        });
        const outsiderAuthentication = {
            withSessionTransaction: (_token, action) => platformTransaction(pool,
                (connection) => action(connection, { userId: randomUUID(), assurance: 2 })),
        };
        await assert.rejects(localUploadStatus(outsiderAuthentication, "fixture", transferReservation.uploadId),
            { code: "publisher.upload_not_found" });
        await pool.execute("UPDATE organization_memberships SET role='member' WHERE organization_id=? AND user_id=?",
            [organizationId, userId]);
        await assert.rejects(localUploadStatus(authentication, "fixture", transferReservation.uploadId),
            { code: "publisher.upload_not_found" });
        await pool.execute("UPDATE organization_memberships SET role='owner' WHERE organization_id=? AND user_id=?",
            [organizationId, userId]);
        await pool.execute("UPDATE publishers SET suspended_at=UTC_TIMESTAMP(6) WHERE id=?", [publisherId]);
        await assert.rejects(localUploadStatus(authentication, "fixture", transferReservation.uploadId),
            { code: "publisher.upload_not_found" });
        await pool.execute("UPDATE publishers SET suspended_at=NULL WHERE id=?", [publisherId]);
        checks += 4;
        await assert.rejects(cancelLocalUpload(authentication, "fixture", transferReservation.uploadId),
            { code: "publisher.upload_not_cancellable" });
        await assert.rejects(cancelLocalUpload(outsiderAuthentication, "fixture", reservation.uploadId),
            { code: "publisher.upload_not_found" });
        const cancellationFailure = new Error("Synthetic cancellation audit failure");
        const cancellationAuthentication = {
            withSessionTransaction: (_token, action) => platformTransaction(pool, (connection) => action({
                execute: (sql, parameters) => {
                    if (sql.includes("'marketplace.upload_cancelled'")) {
                        throw cancellationFailure;
                    }
                    return connection.execute(sql, parameters);
                },
            }, actor)),
        };
        await assert.rejects(cancelLocalUpload(cancellationAuthentication, "fixture", reservation.uploadId),
            (error) => error === cancellationFailure);
        assert.equal((await localUploadStatus(authentication, "fixture", reservation.uploadId)).state, "created");
        assert.deepEqual(await cancelLocalUpload(authentication, "fixture", reservation.uploadId),
            { uploadId: reservation.uploadId, state: "expired" });
        assert.deepEqual(await cancelLocalUpload(authentication, "fixture", reservation.uploadId),
            { uploadId: reservation.uploadId, state: "expired" });
        const [cancellationAudits] = await pool.execute(
            "SELECT id FROM platform_audit_events WHERE target_id=? AND action='marketplace.upload_cancelled'",
            [reservation.uploadId]);
        assert.equal(cancellationAudits.length, 1);
        await assert.rejects(transferLocalUpload(authentication, "fixture", reservation.uploadId, store, chunks(bytes)),
            { code: "publisher.upload_not_writable" });
        checks += 8;
        const retry = await reserveLocalUpload(authentication, "fixture", {
            ...input, productId: reservation.productId,
        });
        assert.notEqual(retry.uploadId, reservation.uploadId);
        assert.equal(retry.versionId, reservation.versionId);
        assert.notEqual(retry.storagePath, reservation.storagePath);
        assert.equal((await localUploadStatus(authentication, "fixture", reservation.uploadId)).state, "expired");
        assert.equal((await localUploadStatus(authentication, "fixture", retry.uploadId)).state, "created");
        await cancelLocalUpload(authentication, "fixture", retry.uploadId);
        const raceReservation = await reserveLocalUpload(authentication, "fixture", {
            ...input, productId: reservation.productId, version: "1.0.2",
            expectedSizeBytes: identity.sizeBytes, expectedSha256: identity.sha256,
        });
        let replacement;
        const cancellationDuringTransfer = {
            put: async (reservedIdentity, incoming) => {
                await store.put(reservedIdentity, incoming);
                await cancelLocalUpload(authentication, "fixture", raceReservation.uploadId);
                replacement = await reserveLocalUpload(authentication, "fixture", {
                    ...input, productId: reservation.productId, version: "1.0.2",
                    expectedSizeBytes: identity.sizeBytes, expectedSha256: identity.sha256,
                });
            },
        };
        await assert.rejects(transferLocalUpload(authentication, "fixture", raceReservation.uploadId,
            cancellationDuringTransfer, chunks(bytes)), { code: "publisher.upload_not_writable" });
        assert.equal((await localUploadStatus(authentication, "fixture", replacement.uploadId)).state, "created");
        assert.equal((await localUploadStatus(authentication, "fixture", raceReservation.uploadId)).state, "expired");
        await transferLocalUpload(authentication, "fixture", replacement.uploadId, store, chunks(bytes));
        assert.equal((await localUploadStatus(authentication, "fixture", replacement.uploadId)).state, "uploaded");
        checks += 9;
        await assert.rejects(transferLocalUpload(authentication, "fixture", transferReservation.uploadId,
            store, chunks(bytes)), { code: "publisher.upload_not_writable" });
        checks += 12;
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
        if (path.dirname(path.resolve(temporaryRoot)) !== path.resolve(tmpdir()) ||
            !path.basename(temporaryRoot).startsWith("keire-upload-transfer-")) {
            throw new Error("Refusing cleanup outside the isolated upload fixture directory");
        }
        await rm(temporaryRoot, { recursive: true, force: true });
    }
    const [remaining] = await pool.execute("SELECT id FROM publishers WHERE id=?", [publisherId]);
    assert.equal(remaining.length, 0);
    return checks + 1;
}
