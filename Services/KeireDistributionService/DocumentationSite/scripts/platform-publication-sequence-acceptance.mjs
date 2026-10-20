import assert from "node:assert/strict";
import { allocatePublicationSequence, publicationSequenceSchema } from "../Source/lib/platform-publication-sequence.mjs";

export async function verifyPublicationSequence(pool) {
    const raw = await pool.getConnection();
    const names = ["platform_publication_sequence", "marketplace_publication_jobs", "marketplace_publications"];
    const connection = {
        execute: (sql, parameters) => raw.execute(
            sql.replace(/\b(platform_publication_sequence|marketplace_publication_jobs|marketplace_publications)\b/g,
                (table) => `${table}_qa_counter`), parameters),
    };
    async function allocate(commit = true) {
        await raw.beginTransaction();
        try {
            const value = await allocatePublicationSequence(connection);
            if (commit) {
                await raw.commit();
            } else {
                await raw.rollback();
            }
            return value;
        } catch (error) {
            await raw.rollback();
            throw error;
        }
    }
    try {
        await connection.execute(publicationSequenceSchema.replace("CREATE TABLE IF NOT EXISTS", "CREATE TEMPORARY TABLE"));
        for (const table of names.slice(1)) {
            await raw.execute(`CREATE TEMPORARY TABLE ${table}_qa_counter LIKE ${table}`);
        }
        await connection.execute(`INSERT INTO marketplace_publication_jobs
            (id,submission_id,version_id,approved_by,publication_sequence,publication_expires_at,
             state,attempt_count,created_at,updated_at)
            VALUES (UUID(),UUID(),UUID(),UUID(),7,DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 10 YEAR),
                'queued',0,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`);
        await connection.execute(`INSERT INTO marketplace_publications
            (id,version_id,artifact_sha256,manifest_sha256,signature_key_id,signed_manifest,approved_by,published_at)
            VALUES (UUID(),UUID(),?,?,'fixture',?,UUID(),UTC_TIMESTAMP(6))`,
        ["a".repeat(64), "b".repeat(64), JSON.stringify({ document: '{"sequence":11}' })]);
        assert.equal(await allocate(), "12");
        assert.equal(await allocate(false), "13");
        assert.equal(await allocate(), "13");
        await connection.execute("UPDATE platform_publication_sequence SET counter_value='9007199254740993' WHERE id=1");
        assert.equal(await allocate(), "9007199254740994");
        await connection.execute("UPDATE platform_publication_sequence SET counter_value='9223372036854775807' WHERE id=1");
        await assert.rejects(allocate(), /exhausted/);
        const [[counter]] = await connection.execute("SELECT CAST(counter_value AS CHAR) AS value FROM platform_publication_sequence");
        assert.equal(counter.value, "9223372036854775807");
        return 6;
    } finally {
        raw.destroy();
    }
}
