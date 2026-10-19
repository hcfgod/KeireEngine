import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { createPlatformPool } from "../Source/lib/platform-mysql.mjs";
import { marketplaceSections, verifyMarketplaceTableCoverage } from "../Source/lib/platform-marketplace-schema.mjs";
import { marketplaceRecords, verifyMarketplaceRecords, verifyMarketplaceSourceColumns } from "../Source/lib/platform-marketplace-import.mjs";
import { verifyMigrationRelationships, verifyMarketplaceConditionalKeys } from "../Source/lib/platform-migration-preflight.mjs";

let pool;
let stage = "input";
try {
    process.stdin.setEncoding("utf8");
    let input = "";
    for await (const chunk of process.stdin) {
        input += chunk;
        if (Buffer.byteLength(input) > 16 * 1024 * 1024) {
            throw new Error("Coverage input exceeds the size limit.");
        }
    }
    const request = JSON.parse(input);
    stage = "verify-export";
    assert.equal(createHash("sha256").update(request.rawSnapshot).digest("hex"), request.expectedDigest);
    assert.equal(createHash("sha256").update(request.rawSchema).digest("hex"), request.schemaDigest);
    const snapshot = JSON.parse(request.rawSnapshot);
    const schema = JSON.parse(request.rawSchema);
    const relationships = verifyMigrationRelationships(snapshot, schema);
    const conditionalKeys = verifyMarketplaceConditionalKeys(snapshot);
    stage = "table-coverage";
    const tableCount = verifyMarketplaceTableCoverage(snapshot);
    pool = await createPlatformPool(request.settings, "runtime");
    let rowCount = 0;
    for (const section of marketplaceSections) {
        stage = `verify-${section}`;
        verifyMarketplaceSourceColumns(schema, section);
        const counts = await verifyMarketplaceRecords(pool, marketplaceRecords(snapshot, section));
        const [ledger] = await pool.execute("SELECT row_counts FROM platform_import_batches WHERE source_digest = ? AND section = ?",
            [request.expectedDigest, section]);
        assert.equal(ledger.length, 1);
        assert.deepEqual(ledger[0].row_counts, counts);
        rowCount += Object.values(counts).reduce((sum, count) => sum + Number(count), 0);
    }
    console.log(JSON.stringify({ status: "all-domain-records-verified", tables: tableCount, rows: rowCount,
        sections: marketplaceSections.length, ...relationships, ...conditionalKeys, databaseModified: false,
        storageBodies: "pending", serviceAcceptance: "pending", liveBackendChanged: false }));
} catch (error) {
    console.error(JSON.stringify({ status: "failed", stage, code: "COVERAGE_FAILED" }));
    process.exitCode = 1;
} finally {
    await pool?.end();
}
