import assert from "node:assert/strict";
import { leaseValidationUpload, renewValidationLease } from "../Source/lib/platform-validator-leases.mjs";

/** Connection-local copies prevent the queue test from leasing any imported customer uploads. */
export async function verifyValidatorLeases(pool, fixtureUploadId, fixtureVersionId) {
    const rawConnection = await pool.getConnection();
    const tables = ["marketplace_uploads", "marketplace_product_versions",
        "marketplace_validation_reports", "platform_audit_events"];
    const connection = {
        execute: (sql, parameters) => rawConnection.execute(
            sql.replace(/\b(marketplace_uploads|marketplace_product_versions|marketplace_validation_reports|platform_audit_events)\b/g,
                (table) => `${table}_qa_lease`), parameters),
        beginTransaction: () => rawConnection.beginTransaction(),
        commit: () => rawConnection.commit(),
        rollback: () => rawConnection.rollback(),
        destroy: () => rawConnection.destroy(),
    };
    const isolatedPool = {
        getConnection: async () => ({
            execute: (...args) => connection.execute(...args),
            beginTransaction: () => connection.beginTransaction(),
            commit: () => connection.commit(),
            rollback: () => connection.rollback(),
            release() {},
            destroy: () => connection.destroy(),
        }),
    };
    try {
        for (const table of tables) {
            await rawConnection.execute(`CREATE TEMPORARY TABLE ${table}_qa_lease LIKE ${table}`);
        }
        for (const [table, id] of [["marketplace_uploads", fixtureUploadId],
            ["marketplace_product_versions", fixtureVersionId]]) {
            const [columns] = await rawConnection.execute(`SHOW COLUMNS FROM ${table}`);
            const writable = columns.filter((column) => !/VIRTUAL GENERATED|STORED GENERATED/.test(column.Extra));
            assert.ok(writable.every((column) => /^[a-z_][a-z0-9_]*$/.test(column.Field)));
            const names = writable.map((column) => `\`${column.Field}\``).join(",");
            await rawConnection.execute(`INSERT INTO ${table}_qa_lease (${names})
                SELECT ${names} FROM ${table} WHERE id=?`, [id]);
        }
        await connection.execute(`UPDATE marketplace_uploads SET state='uploaded',validation_attempts=0,
            lease_owner=NULL,lease_expires_at=NULL,expires_at=TIMESTAMPADD(HOUR,1,UTC_TIMESTAMP(6))`);
        await connection.execute("UPDATE marketplace_product_versions SET state='uploaded'");
        const lease = await leaseValidationUpload(isolatedPool, "validator-fixture", 60);
        assert.equal(lease.uploadId, fixtureUploadId);
        assert.equal(lease.versionId, fixtureVersionId);
        assert.equal(lease.evidenceStoragePath, `${fixtureUploadId}/review-evidence-${lease.expectedSha256}.json`);
        assert.ok(Date.parse(lease.leaseExpiresAt) > Date.now());
        assert.equal(await leaseValidationUpload(isolatedPool, "another-worker", 60), null);
        await assert.rejects(renewValidationLease(isolatedPool, fixtureUploadId, "another-worker", 60), /lease was lost/);
        const renewed = await renewValidationLease(isolatedPool, fixtureUploadId, "validator-fixture", 120);
        assert.ok(Date.parse(renewed.leaseExpiresAt) > Date.parse(lease.leaseExpiresAt));
        const [[active]] = await connection.execute("SELECT state,validation_attempts FROM marketplace_uploads");
        assert.equal(active.state, "leased");
        assert.equal(active.validation_attempts, 1);
        await connection.execute(`UPDATE marketplace_uploads
            SET lease_expires_at=TIMESTAMPADD(SECOND,-1,UTC_TIMESTAMP(6))`);
        await assert.rejects(renewValidationLease(isolatedPool, fixtureUploadId, "validator-fixture", 60), /lease was lost/);
        assert.equal((await leaseValidationUpload(isolatedPool, "replacement-worker", 60)).uploadId, fixtureUploadId);
        const [[retried]] = await connection.execute("SELECT validation_attempts FROM marketplace_uploads");
        assert.equal(retried.validation_attempts, 2);
        await connection.execute(`UPDATE marketplace_uploads SET validation_attempts=5,
            lease_expires_at=TIMESTAMPADD(SECOND,-1,UTC_TIMESTAMP(6))`);
        assert.equal(await leaseValidationUpload(isolatedPool, "replacement-worker", 60), null);
        const [[exhausted]] = await connection.execute("SELECT state,lease_owner FROM marketplace_uploads");
        assert.equal(exhausted.state, "failed");
        assert.equal(exhausted.lease_owner, null);
        const [[version]] = await connection.execute("SELECT state FROM marketplace_product_versions");
        assert.equal(version.state, "validation_failed");
        const [events] = await connection.execute(`SELECT id FROM platform_audit_events
            WHERE action='marketplace.validation.retry_exhausted'`);
        assert.equal(events.length, 1);
        assert.equal(await leaseValidationUpload(isolatedPool, "replacement-worker", 60), null);
        return 18;
    } finally {
        // Closing the connection also removes its private temporary tables, including on assertion failure.
        connection.destroy();
    }
}
