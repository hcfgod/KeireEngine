import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { createPlatformPool, platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { createMarketplaceSchema } from "../Source/lib/platform-marketplace-schema.mjs";
import { importMarketplaceSnapshot, marketplaceRecords, verifyMarketplaceRecords, verifyMarketplaceSourceColumns } from "../Source/lib/platform-marketplace-import.mjs";
import { verifyMigrationRelationships, verifyMarketplaceConditionalKeys } from "../Source/lib/platform-migration-preflight.mjs";
import { verifyAccessConstraints } from "./platform-marketplace-access-acceptance.mjs";
import { verifyContentConstraints } from "./platform-marketplace-content-acceptance.mjs";
import { verifyPublicationConstraints } from "./platform-marketplace-publication-acceptance.mjs";

let pool;
let stage = "input";
let constraintErrorCode;
try {
    let input = "";
    process.stdin.setEncoding("utf8");
    for await (const chunk of process.stdin) {
        input += chunk;
        if (Buffer.byteLength(input) > 16 * 1024 * 1024) {
            throw new Error("Migration request exceeds its size limit.");
        }
    }
    const request = JSON.parse(input);
    const section = request.section ?? "marketplace-core";
    stage = "source-preflight";
    assert.equal(createHash("sha256").update(request.rawSnapshot).digest("hex"), request.expectedDigest);
    assert.equal(createHash("sha256").update(request.rawSchema).digest("hex"), request.schemaDigest);
    const snapshot = JSON.parse(request.rawSnapshot);
    const schema = JSON.parse(request.rawSchema);
    verifyMigrationRelationships(snapshot, schema);
    verifyMarketplaceSourceColumns(schema, section);
    verifyMarketplaceConditionalKeys(snapshot);
    const records = marketplaceRecords(snapshot, section);
    pool = await createPlatformPool(request.settings, "migration");
    const verifyOnly = process.argv.includes("--verify-only");
    const acceptanceOnly = process.argv.includes("--acceptance-only");
    assert.equal(verifyOnly && acceptanceOnly, false);
    if (process.argv.includes("--create-schema")) {
        assert.equal(verifyOnly, false);
        assert.equal(acceptanceOnly, false);
        stage = `create-${section}`;
        await createMarketplaceSchema(pool, section);
    }
    stage = `import-${section}`;
    if (!verifyOnly && !acceptanceOnly) {
        await importMarketplaceSnapshot(pool, request.rawSnapshot, request.expectedDigest, section);
    }
    stage = `verify-${section}`;
    const counts = await verifyMarketplaceRecords(pool, records);
    const [batches] = await pool.execute(`SELECT row_counts FROM platform_import_batches
        WHERE source_digest = ? AND section = ?`, [request.expectedDigest, section]);
    assert.equal(batches.length, 1);
    assert.deepEqual(batches[0].row_counts, counts);
    let constraintChecks = 0;
    if (!verifyOnly && section === "marketplace-core") {
        stage = "constraint-acceptance";
        const rollback = new Error("intentional constraint-test rollback");
        await assert.rejects(platformTransaction(pool, async (connection) => {
            const reject = async (label, sql, code) => {
                stage = `constraint-${label}`;
                constraintErrorCode = undefined;
                await assert.rejects(connection.query(sql), (error) => {
                    constraintErrorCode = error.code;
                    return (Array.isArray(code) ? code : [code]).includes(error.code);
                });
                constraintChecks++;
            };
            await reject("category-slug", "UPDATE marketplace_categories SET slug = 'INVALID' LIMIT 1", "ER_CHECK_CONSTRAINT_VIOLATED");
            await reject("publisher-reference", "UPDATE marketplace_products SET publisher_id = '00000000-0000-0000-0000-000000000000' LIMIT 1",
                ["ER_NO_REFERENCED_ROW", "ER_NO_REFERENCED_ROW_2"]);
            await reject("offer-price", "UPDATE marketplace_offers SET amount_minor = 1 LIMIT 1", "ER_CHECK_CONSTRAINT_VIOLATED");
            await reject("active-offer", `INSERT INTO marketplace_offers (id,product_id,currency,amount_minor,active,created_at)
                SELECT '00000000-0000-0000-0000-000000000001',product_id,currency,amount_minor,active,created_at
                FROM marketplace_offers WHERE active = 1 LIMIT 1`, "ER_DUP_ENTRY");
            stage = "constraint-offer-cascade";
            const product = records.find((record) => record.table.name === "marketplace_products");
            const fixtureProductId = randomUUID();
            const fixtureValues = [...product.values[0]];
            fixtureValues[product.names.indexOf("id")] = fixtureProductId;
            fixtureValues[product.names.indexOf("slug")] = `qa-${fixtureProductId}`;
            await connection.execute(`INSERT INTO marketplace_products (${product.names.map((name) => `\`${name}\``).join(",")})
                VALUES (${fixtureValues.map(() => "?").join(",")})`, fixtureValues);
            await connection.execute(`INSERT INTO marketplace_offers (id,product_id,currency,amount_minor,active,created_at)
                VALUES (?,?,'USD',0,1,UTC_TIMESTAMP(6))`, [randomUUID(), fixtureProductId]);
            await connection.execute("DELETE FROM marketplace_products WHERE id = ?", [fixtureProductId]);
            const [offers] = await connection.execute("SELECT COUNT(*) AS count FROM marketplace_offers WHERE product_id = ?", [fixtureProductId]);
            assert.equal(Number(offers[0].count), 0);
            constraintChecks++;
            stage = "constraint-write-rollback";
            await connection.query("UPDATE marketplace_categories SET sort_order = sort_order + 1 LIMIT 1");
            throw rollback;
        }), (error) => error === rollback);
        await verifyMarketplaceRecords(pool, records);
        constraintChecks++;
    }
    if (!verifyOnly && section === "marketplace-access") {
        stage = "access-constraint-acceptance";
        constraintChecks = await verifyAccessConstraints(pool, records);
    }
    if (!verifyOnly && section === "marketplace-content") {
        stage = "content-constraint-acceptance";
        constraintChecks = await verifyContentConstraints(pool, records);
    }
    if (!verifyOnly && section === "marketplace-publication") {
        stage = "publication-constraint-acceptance";
        constraintChecks = await verifyPublicationConstraints(pool, records);
    }
    console.log(JSON.stringify({ status: "marketplace-import-verified", section, counts, constraintChecks,
        verificationOnly: verifyOnly, liveBackendChanged: false, fullMarketplaceAcceptance: "pending" }));
} catch (error) {
    console.error(JSON.stringify({ status: "failed", stage,
        code: /^[A-Z_0-9]+$/.test(error.code ?? "") ? error.code : "VALIDATION_FAILED",
        constraintCode: /^[A-Z_0-9]+$/.test(constraintErrorCode ?? "") ? constraintErrorCode : null }));
    process.exitCode = 1;
} finally {
    await pool?.end();
}
