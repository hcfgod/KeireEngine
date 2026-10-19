import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { canonicalJson } from "./platform-marketplace-import.mjs";
import { planStorageRecovery } from "./platform-storage-plan.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

export const storageSchema = [
    `CREATE TABLE platform_storage_buckets (
        id VARCHAR(255) NOT NULL PRIMARY KEY,
        is_public BOOLEAN NOT NULL,
        source_record JSON NOT NULL,
        CHECK (is_public IN (0,1)),
        CHECK (JSON_TYPE(source_record) = 'OBJECT')
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_bin`,
    `CREATE TABLE platform_storage_objects (
        id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL PRIMARY KEY,
        bucket_id VARCHAR(255) NOT NULL,
        object_name TEXT NOT NULL,
        name_digest BINARY(32) GENERATED ALWAYS AS (UNHEX(SHA2(object_name,256))) STORED,
        size_bytes BIGINT NOT NULL,
        expected_sha256 CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
        recovered_sha256 CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NULL,
        recovered_at DATETIME(6) NULL,
        source_record JSON NOT NULL,
        UNIQUE KEY storage_name (bucket_id,name_digest),
        FOREIGN KEY (bucket_id) REFERENCES platform_storage_buckets(id),
        CHECK (size_bytes >= 0),
        CHECK (expected_sha256 IS NULL OR REGEXP_LIKE(expected_sha256,'^[0-9a-f]{64}$','c')),
        CHECK ((recovered_sha256 IS NULL AND recovered_at IS NULL) OR
            (recovered_sha256 IS NOT NULL AND recovered_at IS NOT NULL AND expected_sha256 IS NOT NULL
                AND recovered_sha256 = expected_sha256)),
        CHECK (JSON_TYPE(source_record) = 'OBJECT')
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_bin`,
];

export async function verifyStorageMetadata(connection, plan) {
    const [buckets] = await connection.query("SELECT id,is_public,source_record FROM platform_storage_buckets");
    const [objects] = await connection.query(`SELECT id,bucket_id,object_name,size_bytes,expected_sha256,
        recovered_sha256,recovered_at,source_record FROM platform_storage_objects`);
    assert.equal(buckets.length, plan.buckets.length);
    assert.equal(objects.length, plan.objects.length);
    const bucketMap = new Map(buckets.map((row) => [row.id, row]));
    const objectMap = new Map(objects.map((row) => [row.id, row]));
    for (const source of plan.buckets) {
        const row = bucketMap.get(source.id);
        assert.ok(row);
        assert.equal(row.is_public, Number(source.public));
        assert.equal(canonicalJson(row.source_record), canonicalJson(source));
    }
    for (const entry of plan.objects) {
        const row = objectMap.get(entry.source.id);
        assert.ok(row);
        assert.equal(row.bucket_id, entry.source.bucket_id);
        assert.equal(row.object_name, entry.source.name);
        assert.equal(String(row.size_bytes), String(entry.sizeBytes));
        assert.equal(row.expected_sha256, entry.expectedSha256);
        assert.equal(row.recovered_sha256, null);
        assert.equal(row.recovered_at, null);
        assert.equal(canonicalJson(row.source_record), canonicalJson(entry.source));
    }
    return { buckets: buckets.length, objects: objects.length, recoveredObjects: 0 };
}

export async function importStorageMetadata(pool, rawSnapshot, expectedDigest) {
    assert.match(expectedDigest, /^[a-f0-9]{64}$/);
    assert.equal(createHash("sha256").update(rawSnapshot).digest("hex"), expectedDigest);
    const plan = planStorageRecovery(JSON.parse(rawSnapshot));
    return platformTransaction(pool, async (connection) => {
        for (const table of ["platform_storage_buckets", "platform_storage_objects"]) {
            const [rows] = await connection.query(`SELECT COUNT(*) AS count FROM ${table}`);
            assert.equal(Number(rows[0].count), 0);
        }
        for (const source of plan.buckets) {
            await connection.execute(`INSERT INTO platform_storage_buckets (id,is_public,source_record)
                VALUES (?,?,?)`, [source.id, Number(source.public), canonicalJson(source)]);
        }
        for (const entry of plan.objects) {
            await connection.execute(`INSERT INTO platform_storage_objects
                (id,bucket_id,object_name,size_bytes,expected_sha256,source_record) VALUES (?,?,?,?,?,?)`,
                [entry.source.id, entry.source.bucket_id, entry.source.name, entry.sizeBytes,
                    entry.expectedSha256, canonicalJson(entry.source)]);
        }
        const counts = await verifyStorageMetadata(connection, plan);
        await connection.execute(`INSERT INTO platform_import_batches (source_digest,source_project,section,row_counts)
            VALUES (?, ?, 'storage-metadata',?)`, [expectedDigest, "khjduyjamzwumhducmou", JSON.stringify(counts)]);
        return counts;
    });
}
