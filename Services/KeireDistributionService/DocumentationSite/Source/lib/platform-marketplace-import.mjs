import { createHash } from "node:crypto";
import { mysqlUtcTimestamp } from "./platform-account-import.mjs";
import { marketplaceTables } from "./platform-marketplace-schema.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

function normalizeValue(value, definition) {
    if (value === null) {
        if (definition.endsWith("NOT NULL")) {
            throw new Error("A required marketplace value is null.");
        }
        return null;
    }
    if (definition.startsWith("DATETIME")) {
        if (typeof value !== "string" || !value.length) {
            throw new Error("Invalid marketplace timestamp.");
        }
        return mysqlUtcTimestamp(value);
    }
    if (definition.startsWith("JSON")) {
        if (!Array.isArray(value) || value.some((item) => item !== null && typeof item !== "string")) {
            throw new Error("A marketplace text array cannot be converted without data loss.");
        }
        return JSON.stringify(value);
    }
    if (definition.startsWith("BOOLEAN")) {
        if (typeof value !== "boolean") {
            throw new Error("Invalid marketplace boolean.");
        }
        return Number(value);
    }
    if (/^(BIGINT|INT)\b/.test(definition)) {
        if ((typeof value === "number" && !Number.isSafeInteger(value)) ||
            !["number", "string"].includes(typeof value) || !/^-?(0|[1-9][0-9]*)$/.test(String(value))) {
            throw new Error("Unsafe marketplace integer; use a lossless source value.");
        }
        const integer = BigInt(value);
        const bits = definition.startsWith("BIGINT") ? 64n : 32n;
        if (integer < -(2n ** (bits - 1n)) || integer >= 2n ** (bits - 1n)) {
            throw new Error("Marketplace integer is out of range.");
        }
        return definition.startsWith("BIGINT") ? String(integer) : Number(integer);
    }
    if (typeof value !== "string") {
        throw new Error("Invalid marketplace text value.");
    }
    if (definition.startsWith("CHAR(36)") && !/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/.test(value)) {
        throw new Error("Invalid marketplace identifier.");
    }
    const bounded = /^(?:VAR)?CHAR\((\d+)\)/.exec(definition);
    if (bounded && [...value].length > Number(bounded[1])) {
        throw new Error("Marketplace text exceeds its supported destination bound; import refused.");
    }
    return value;
}

export function marketplaceRecords(snapshot, section = "marketplace-core") {
    if (snapshot?.project !== "khjduyjamzwumhducmou" || !snapshot.tables) {
        throw new Error("Unexpected marketplace source project.");
    }
    return marketplaceTables(section).map((table) => {
        const rows = snapshot.tables[`public.${table.name}`];
        const names = Object.keys(table.columns);
        if (!Array.isArray(rows)) {
            throw new Error(`Missing source table: ${table.name}.`);
        }
        const values = rows.map((row) => {
            if (!row || Object.keys(row).length !== names.length || !names.every((name) => Object.hasOwn(row, name))) {
                throw new Error(`Source columns changed for ${table.name}; review the mapping before import.`);
            }
            return names.map((name) => normalizeValue(row[name], table.columns[name]));
        });
        return { table, names, values };
    });
}

export async function verifyMarketplaceRecords(connection, records) {
    const counts = {};
    for (const { table, names, values } of records) {
        const [rows] = await connection.query(`SELECT ${names.map((name) => `\`${name}\``).join(",")} FROM \`${table.name}\``);
        const actual = rows.map((row) => JSON.stringify(names.map((name) => {
            const value = row[name];
            return table.columns[name].startsWith("JSON") && value !== null ? JSON.stringify(value) : value;
        }))).sort();
        const expected = values.map((row) => JSON.stringify(row)).sort();
        if (JSON.stringify(actual) !== JSON.stringify(expected)) {
            throw new Error(`Marketplace verification mismatch in ${table.name}.`);
        }
        counts[table.name] = rows.length;
    }
    return counts;
}

export async function importMarketplaceSnapshot(pool, rawSnapshot, expectedDigest, section = "marketplace-core") {
    if (!/^[a-f0-9]{64}$/.test(expectedDigest) ||
        createHash("sha256").update(rawSnapshot).digest("hex") !== expectedDigest) {
        throw new Error("Marketplace snapshot digest mismatch.");
    }
    const snapshot = JSON.parse(rawSnapshot);
    const records = marketplaceRecords(snapshot, section);
    return platformTransaction(pool, async (connection) => {
        for (const { table, names, values } of records) {
            const [existing] = await connection.query(`SELECT COUNT(*) AS count FROM \`${table.name}\``);
            if (Number(existing[0].count) !== 0) {
                throw new Error("Marketplace import requires empty destination tables; existing rows were preserved.");
            }
            const insert = `INSERT INTO \`${table.name}\` (${names.map((name) => `\`${name}\``).join(",")}) ` +
                `VALUES (${names.map(() => "?").join(",")})`;
            for (const row of values) {
                await connection.execute(insert, row);
            }
        }
        const counts = await verifyMarketplaceRecords(connection, records);
        await connection.execute(`INSERT INTO platform_import_batches
            (source_digest, source_project, section, row_counts) VALUES (?, ?, ?, ?)`,
        [expectedDigest, snapshot.project, section, JSON.stringify(counts)]);
        return counts;
    });
}
