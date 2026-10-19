import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { createPlatformPool, platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { planStorageRecovery } from "../Source/lib/platform-storage-plan.mjs";
import { storageSchema, importStorageMetadata, verifyStorageMetadata } from "../Source/lib/platform-storage-import.mjs";

let pool;
let stage = "input";
try {
    let input = "";
    process.stdin.setEncoding("utf8");
    for await (const chunk of process.stdin) {
        input += chunk;
        assert.ok(Buffer.byteLength(input) <= 16 * 1024 * 1024);
    }
    const request = JSON.parse(input);
    const verifyOnly = process.argv.includes("--verify-only");
    const createSchema = process.argv.includes("--create-schema");
    assert.equal(verifyOnly && createSchema, false);
    stage = "preflight";
    assert.equal(createHash("sha256").update(request.rawSnapshot).digest("hex"), request.expectedDigest);
    const plan = planStorageRecovery(JSON.parse(request.rawSnapshot));
    pool = await createPlatformPool(request.settings, verifyOnly ? "runtime" : "migration");
    if (createSchema) {
        stage = "schema";
        for (const sql of storageSchema) {
            await pool.query(sql);
        }
    }
    if (!verifyOnly) {
        stage = "import";
        await importStorageMetadata(pool, request.rawSnapshot, request.expectedDigest);
    }
    stage = "verify";
    const counts = await verifyStorageMetadata(pool, plan);
    const [ledger] = await pool.execute(`SELECT row_counts FROM platform_import_batches
        WHERE source_digest=? AND section='storage-metadata'`, [request.expectedDigest]);
    assert.equal(ledger.length, 1);
    assert.deepEqual(ledger[0].row_counts, counts);
    let constraintChecks = 0;
    if (!verifyOnly) {
        stage = "constraints";
        const rollback = new Error("rollback storage fixtures");
        await assert.rejects(platformTransaction(pool, async (connection) => {
            for (const sql of [
                "UPDATE platform_storage_objects SET size_bytes=-1 LIMIT 1",
                "UPDATE platform_storage_objects SET expected_sha256='invalid' LIMIT 1",
                "UPDATE platform_storage_objects SET recovered_at=UTC_TIMESTAMP(6) LIMIT 1",
                "UPDATE platform_storage_buckets SET is_public=2 LIMIT 1",
            ]) {
                await assert.rejects(connection.query(sql), (error) => error.code === "ER_CHECK_CONSTRAINT_VIOLATED");
                constraintChecks++;
            }
            await assert.rejects(connection.query("UPDATE platform_storage_objects SET bucket_id='missing-fixture' LIMIT 1"),
                (error) => ["ER_NO_REFERENCED_ROW", "ER_NO_REFERENCED_ROW_2"].includes(error.code));
            constraintChecks++;
            await connection.query("UPDATE platform_storage_buckets SET is_public=1-is_public LIMIT 1");
            throw rollback;
        }), (error) => error === rollback);
        await verifyStorageMetadata(pool, plan);
        constraintChecks++;
    }
    console.log(JSON.stringify({ status: "storage-metadata-verified", counts, constraintChecks, summary: plan.summary,
        liveBackendChanged: false }));
} catch (error) {
    console.error(JSON.stringify({ status: "failed", stage,
        code: /^[A-Z_0-9]+$/.test(error.code ?? "") ? error.code : "VALIDATION_FAILED" }));
    process.exitCode = 1;
} finally {
    await pool?.end();
}
