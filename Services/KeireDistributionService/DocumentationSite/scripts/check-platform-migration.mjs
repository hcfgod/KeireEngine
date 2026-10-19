import { createHash } from "node:crypto";
import { verifyMigrationRelationships, verifyMarketplaceConditionalKeys } from "../Source/lib/platform-migration-preflight.mjs";

let stage = "read-input";
try {
    let input = "";
    for await (const chunk of process.stdin) {
        input += chunk.toString("utf8");
        if (Buffer.byteLength(input) > 16 * 1024 * 1024) {
            throw new Error("Migration input exceeds the preflight limit.");
        }
    }
    const request = JSON.parse(input);
    stage = "verify-digests";
    for (const name of ["database", "schema"]) {
        if (typeof request[name] !== "string" ||
            createHash("sha256").update(request[name]).digest("hex") !== request[`${name}Digest`]) {
            throw new Error("Migration export digest mismatch.");
        }
    }
    const database = JSON.parse(request.database);
    const schema = JSON.parse(request.schema);
    stage = "relationships";
    const relationships = verifyMigrationRelationships(database, schema);
    stage = "conditional-uniqueness";
    const conditional = verifyMarketplaceConditionalKeys(database);
    console.log(JSON.stringify({ status: "migration-data-preflight-passed", ...relationships, ...conditional,
        databaseModified: false, destinationSchemaAcceptance: "pending" }));
} catch (error) {
    // Never emit input records, keys, or SQL driver diagnostics.
    console.error(JSON.stringify({ status: "failed", stage }));
    process.exitCode = 1;
}
