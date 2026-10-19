import assert from "node:assert/strict";
import { createPlatformPool, platformTransaction } from "../Source/lib/platform-mysql.mjs";

// The protected Windows launcher supplies settings over stdin, never command-line arguments or a plaintext file.
let settingsText = "";
for await (const chunk of process.stdin) {
    settingsText += chunk.toString("utf8");
    if (settingsText.length > 65536) {
        throw new Error("Database test settings exceeded the input limit.");
    }
}
const settings = JSON.parse(settingsText);
settingsText = "";
const runtime = await createPlatformPool(settings);
const migration = await createPlatformPool(settings, "migration");
try {
    for (const pool of [runtime, migration]) {
        const [rows] = await pool.execute("SELECT DATABASE() AS name, CAST('9223372036854775807' AS UNSIGNED) AS large_value");
        assert.equal(rows[0].name, "keire_platform");
        assert.equal(rows[0].large_value, "9223372036854775807");
        const [transport] = await pool.query("SHOW SESSION STATUS LIKE 'Ssl_cipher'");
        assert.notEqual(transport[0].Value, "");
    }
    const hostileValue = "'; DROP DATABASE aether_platform; --";
    const [literal] = await runtime.execute("SELECT ? AS literal_value", [hostileValue]);
    assert.equal(literal[0].literal_value, hostileValue);

    let otherDatabaseDenied = false;
    try {
        await runtime.query("SHOW TABLES FROM aether_platform");
    } catch (error) {
        otherDatabaseDenied = error.code === "ER_DBACCESS_DENIED_ERROR";
    }
    assert.equal(otherDatabaseDenied, true, "Runtime account must not read Aether's schema.");

    const expected = new Error("deliberate rollback probe");
    await assert.rejects(platformTransaction(runtime, async (connection) => {
        await connection.execute("SELECT ? AS probe", ["rollback"]);
        throw expected;
    }), (error) => error === expected);
    await platformTransaction(runtime, async (connection) => {
        await connection.execute("SELECT ? AS probe", ["commit"]);
    });
    console.log("PASS: runtime/migration TLS, exact integers, prepared parameters, cross-database denial, transaction reuse.");
    console.log("No tables or application records were created or changed by this check.");
} catch (error) {
    // Driver errors may contain SQL or values. Keep credentials and data out of diagnostic output.
    console.error("Platform database acceptance failed; inspect the test locally without logging connection settings.");
    process.exitCode = 1;
} finally {
    await Promise.allSettled([runtime.end(), migration.end()]);
}
