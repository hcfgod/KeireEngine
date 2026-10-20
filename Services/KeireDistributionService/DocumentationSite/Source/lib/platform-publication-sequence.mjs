const maximumSequence = 9223372036854775807n;

export const publicationSequenceSchema = `CREATE TABLE IF NOT EXISTS platform_publication_sequence (
    id TINYINT NOT NULL PRIMARY KEY,
    counter_value BIGINT NOT NULL,
    CHECK (id = 1),
    CHECK (counter_value >= 0)
) ENGINE=InnoDB`;

function sequenceValue(value) {
    if (typeof value !== "string" || !/^(0|[1-9][0-9]*)$/.test(value)) {
        throw new Error("The publication sequence is invalid.");
    }
    const parsed = BigInt(value);
    if (parsed > maximumSequence) {
        throw new Error("The publication sequence exceeds the supported range.");
    }
    return parsed;
}

/**
 * Call inside the approval transaction. All local job writers must use this allocator.
 * Imported jobs/publications remain the floor; the counter also preserves cancelled allocations.
 */
export async function allocatePublicationSequence(connection) {
    await connection.execute(`INSERT INTO platform_publication_sequence (id,counter_value)
        VALUES (1,0) ON DUPLICATE KEY UPDATE id=id`);
    const [[counter]] = await connection.execute(
        "SELECT CAST(counter_value AS CHAR) AS counter_value FROM platform_publication_sequence WHERE id=1 FOR UPDATE");
    const [[jobs]] = await connection.execute(
        "SELECT CAST(COALESCE(MAX(publication_sequence),0) AS CHAR) AS maximum FROM marketplace_publication_jobs");
    const [publications] = await connection.execute(`SELECT JSON_UNQUOTE(JSON_EXTRACT(
        JSON_UNQUOTE(JSON_EXTRACT(signed_manifest,'$.document')),'$.sequence')) AS sequence
        FROM marketplace_publications`);
    const floor = [counter.counter_value, jobs.maximum, ...publications.map((row) => row.sequence)]
        .map(sequenceValue).reduce((highest, value) => value > highest ? value : highest, 0n);
    if (floor === maximumSequence) {
        throw new Error("The publication sequence is exhausted.");
    }
    const next = (floor + 1n).toString();
    await connection.execute("UPDATE platform_publication_sequence SET counter_value=? WHERE id=1", [next]);
    return next;
}
