import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { allocatePublicationSequence, publicationSequenceSchema } from "../Source/lib/platform-publication-sequence.mjs";

/** Separate uniquely named fixture tables permit real cross-connection locking without touching release data. */
export async function verifyPublicationConcurrency(pool) {
    const cleanupSettings = [
        process.env.KEIRE_QA_POWERSHELL,
        process.env.KEIRE_QA_BOOTSTRAP_CREDENTIAL,
        process.env.KEIRE_QA_BOOTSTRAP_CA,
    ];
    if (cleanupSettings.some((value) => !path.isAbsolute(value ?? ""))
        || !process.env.KEIRE_QA_BOOTSTRAP_ENTROPY) {
        throw new Error("Configure scoped publication fixture cleanup before creating shared tables.");
    }
    const suffix = randomUUID().replaceAll("-", "");
    const tables = new Map([
        ["platform_publication_sequence", `qa_counter_${suffix}`],
        ["marketplace_publication_jobs", `qa_jobs_${suffix}`],
        ["marketplace_publications", `qa_publications_${suffix}`],
    ]);
    const rewrite = (sql) => sql.replace(
        /\b(platform_publication_sequence|marketplace_publication_jobs|marketplace_publications)\b/g,
        (table) => tables.get(table));
    const created = [];
    const connections = [];
    try {
        const counter = tables.get("platform_publication_sequence");
        // No IF NOT EXISTS: a collision must fail, never adopt another fixture's table.
        await pool.execute(rewrite(publicationSequenceSchema.replace(" IF NOT EXISTS", "")));
        created.push(counter);
        for (const source of ["marketplace_publication_jobs", "marketplace_publications"]) {
            const target = tables.get(source);
            await pool.execute(`CREATE TABLE ${target} LIKE ${source}`);
            created.push(target);
        }
        connections.push(await pool.getConnection());
        connections.push(await pool.getConnection());
        async function worker(raw) {
            const allocated = [];
            for (let iteration = 0; iteration < 5; iteration++) {
                await raw.beginTransaction();
                try {
                    const value = await allocatePublicationSequence({
                        execute: (sql, parameters) => raw.execute(rewrite(sql), parameters),
                    });
                    await raw.commit();
                    allocated.push(Number(value));
                } catch (error) {
                    await raw.rollback();
                    throw error;
                }
            }
            return allocated;
        }
        const outcomes = await Promise.allSettled(connections.map(worker));
        for (const outcome of outcomes) {
            if (outcome.status === "rejected") {
                throw outcome.reason;
            }
        }
        const values = outcomes.flatMap((outcome) => outcome.value).sort((left, right) => left - right);
        assert.deepEqual(values, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
        const [[row]] = await pool.execute(`SELECT counter_value FROM ${counter} WHERE id=1`);
        assert.equal(String(row.counter_value), "10");
        return 2;
    } finally {
        for (const connection of connections) {
            connection.release();
        }
        if (created.length) {
            try {
                const result = await promisify(execFile)(cleanupSettings[0], [
                    "-NoProfile", "-NonInteractive", "-File",
                    fileURLToPath(new URL("../../scripts/remove-local-publication-qa-tables.ps1", import.meta.url)),
                    "-FixtureId", suffix, "-BootstrapCredentialPath", cleanupSettings[1],
                    "-BootstrapEntropy", process.env.KEIRE_QA_BOOTSTRAP_ENTROPY,
                    "-CertificateAuthorityPath", cleanupSettings[2],
                ], { windowsHide: true, shell: false, timeout: 35000, maxBuffer: 4096 });
                assert.equal(JSON.parse(result.stdout.trim()).status, "publication-fixtures-removed");
            } catch {
                throw new Error(`Publication fixture cleanup failed for ${suffix}.`);
            }
        }
    }
}
