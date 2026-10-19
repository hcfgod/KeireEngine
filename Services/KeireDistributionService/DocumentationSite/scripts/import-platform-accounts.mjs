import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { createPlatformPool, platformTransaction } from "../Source/lib/platform-mysql.mjs";
import {
    accountImportRecords, createInitialAccountSchema, importAccountSnapshot, mysqlUtcTimestamp, openAccountRecord,
} from "../Source/lib/platform-account-import.mjs";

// Only the local protected-data launcher supplies this input. Never print settings, records, or driver errors.
process.stdin.setEncoding("utf8");
let input = "";
for await (const chunk of process.stdin) {
    input += chunk;
    if (input.length > 4 * 1024 * 1024) {
        throw new Error("Account migration input exceeds the supported size.");
    }
}
const request = JSON.parse(input);
input = "";
const key = Buffer.from(request.encryptionKey, "base64");
const pool = await createPlatformPool(request.settings, "migration");
const verifyOnly = process.argv.includes("--verify-only");
let stage = "preflight";
try {
    stage = "preflight-key";
    assert.equal(key.length, 32);
    stage = "preflight-snapshot-digest";
    assert.equal(createHash("sha256").update(request.rawSnapshot).digest("hex"), request.expectedDigest);
    stage = "preflight-source-records";
    const records = accountImportRecords(JSON.parse(request.rawSnapshot));
    if (process.argv.includes("--create-schema")) {
        assert.equal(verifyOnly, false, "Schema creation cannot be combined with verification only.");
        stage = "create-schema";
        await createInitialAccountSchema(pool);
    }
    stage = "import-accounts";
    const counts = verifyOnly
        ? Object.fromEntries(Object.entries(records).map(([name, rows]) => [name, rows.length]))
        : await importAccountSnapshot(pool, request.rawSnapshot, request.expectedDigest, key);
    stage = "verify-import";
    for (const [table, expected] of [
        ["platform_accounts", records.users.length], ["platform_identities", records.identities.length],
        ["platform_mfa_factors", records.factors.length], ["platform_profiles", records.profiles.length],
    ]) {
        const [rows] = await pool.query(`SELECT COUNT(*) AS count FROM ${table}`);
        assert.equal(Number(rows[0].count), expected);
    }
    for (const account of records.users) {
        const [rows] = await pool.execute(`SELECT password_hash, source_sealed, email_key, created_at, updated_at,
            email_confirmed_at, banned_until, deleted_at FROM platform_accounts WHERE id = ?`, [account.id]);
        assert.equal(rows[0].password_hash, account.encrypted_password || null);
        assert.equal(rows[0].email_key, account.email ? account.email.trim().toLowerCase() : null);
        for (const name of ["created_at", "updated_at", "email_confirmed_at", "banned_until", "deleted_at"]) {
            assert.equal(rows[0][name], mysqlUtcTimestamp(account[name]));
        }
        assert.deepEqual(openAccountRecord(rows[0].source_sealed, key, `account:${account.id}`), account);
    }
    for (const factor of records.factors) {
        const [rows] = await pool.execute("SELECT status, source_sealed FROM platform_mfa_factors WHERE id = ?", [factor.id]);
        assert.equal(rows[0].status, factor.status);
        assert.deepEqual(openAccountRecord(rows[0].source_sealed, key, `mfa:${factor.id}`), factor);
    }
    for (const identity of records.identities) {
        const [rows] = await pool.execute("SELECT source_record FROM platform_identities WHERE id = ?", [identity.id]);
        assert.deepEqual(rows[0].source_record, identity);
    }
    for (const profile of records.profiles) {
        const [rows] = await pool.execute("SELECT source_record FROM platform_profiles WHERE user_id = ?", [profile.user_id]);
        assert.deepEqual(rows[0].source_record, profile);
    }

    const [batches] = await pool.execute("SELECT section, row_counts FROM platform_import_batches WHERE source_digest = ? AND section = 'accounts'",
        [request.expectedDigest]);
    assert.equal(batches.length, 1);
    assert.equal(batches[0].section, "accounts");
    assert.deepEqual(batches[0].row_counts, counts);
    if (!verifyOnly) {
        stage = "verify-write-rollback";
        const probeDigest = createHash("sha256").update("keire-account-import-rollback-probe").digest("hex");
        const rollback = new Error("intentional rollback");
        await assert.rejects(platformTransaction(pool, async (connection) => {
            await connection.execute(`INSERT INTO platform_import_batches
                (source_digest, source_project, section, row_counts) VALUES (?, 'integration-test', 'rollback-probe', '{}')`,
            [probeDigest]);
            throw rollback;
        }), (error) => error === rollback);
        const [probeRows] = await pool.execute("SELECT COUNT(*) AS count FROM platform_import_batches WHERE source_digest = ?", [probeDigest]);
        assert.equal(Number(probeRows[0].count), 0);
    }
    console.log(JSON.stringify({ status: "account-data-import-verified", counts, verificationOnly: verifyOnly,
        rollbackVerified: !verifyOnly,
        liveBackendChanged: false, loginAcceptance: "pending", marketplaceImport: "pending" }));
} catch {
    console.error(JSON.stringify({ status: "failed", stage,
        action: "Inspect the isolated database before retrying. No source data was deleted. Sensitive diagnostics suppressed." }));
    process.exitCode = 1;
} finally {
    key.fill(0);
    request.encryptionKey = null;
    request.rawSnapshot = null;
    await pool.end();
}
