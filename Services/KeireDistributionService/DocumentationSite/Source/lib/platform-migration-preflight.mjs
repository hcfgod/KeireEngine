const identifier = /^[a-z_][a-z0-9_]*$/;

function columns(value) {
    const result = value.split(", ");
    if (!result.every((name) => identifier.test(name))) {
        throw new Error("Unsupported migration key definition.");
    }
    return result;
}

function tableRows(tables, name) {
    if (!Object.hasOwn(tables, name) || !Array.isArray(tables[name])) {
        throw new Error(`Missing migration table: ${name}.`);
    }
    return tables[name];
}

function keyValues(row, names) {
    return names.map((name) => {
        if (!Object.hasOwn(row, name)) {
            throw new Error(`Missing migration column: ${name}.`);
        }
        const value = row[name];
        if (typeof value === "number" && !Number.isSafeInteger(value)) {
            throw new Error("Unsafe numeric migration key; use a lossless export.");
        }
        if (value !== null && !["string", "number", "boolean"].includes(typeof value)) {
            throw new Error("Unsupported migration key value.");
        }
        return value;
    });
}

function assertUnique(rows, names, primary, label, predicate = () => true, lowercase = false) {
    const seen = new Set();
    for (const row of rows) {
        if (!predicate(row)) {
            continue;
        }
        const values = keyValues(row, names);
        if (values.includes(null)) {
            if (primary) {
                throw new Error(`Null primary key in ${label}.`);
            }
            continue;
        }
        const key = JSON.stringify(lowercase ? values.map((value) => value.toLowerCase()) : values);
        if (seen.has(key)) {
            throw new Error(`Duplicate migration key in ${label}.`);
        }
        seen.add(key);
    }
}

/** Checks data relationships only. Does not execute SQL or replace destination authorization/check constraints. */
export function verifyMigrationRelationships(database, schema) {
    if (!database?.tables || !Array.isArray(schema?.constraints)) {
        throw new Error("Invalid migration exports.");
    }
    let foreignKeys = 0;
    let uniqueKeys = 0;
    for (const constraint of schema.constraints) {
        if (!["public", "private"].includes(constraint.schema)) {
            continue;
        }
        const name = `${constraint.schema}.${constraint.table_name}`;
        const rows = tableRows(database.tables, name);
        const definition = constraint.definition;
        const unique = /^(PRIMARY KEY|UNIQUE) \(([^)]+)\)$/.exec(definition);
        if (unique) {
            assertUnique(rows, columns(unique[2]), unique[1] === "PRIMARY KEY", name);
            uniqueKeys++;
            continue;
        }
        if (!definition.startsWith("FOREIGN KEY")) {
            continue;
        }
        const foreign = /^FOREIGN KEY \(([^)]+)\) REFERENCES (?:(public|private|auth)\.)?([a-z_][a-z0-9_]*)\(([^)]+)\)(?: ON (?:DELETE|UPDATE) (?:CASCADE|RESTRICT|SET NULL|NO ACTION))*$/.exec(definition);
        if (!foreign) {
            throw new Error(`Unsupported foreign key in ${name}.`);
        }
        const sourceColumns = columns(foreign[1]);
        const targetColumns = columns(foreign[4]);
        if (sourceColumns.length !== targetColumns.length) {
            throw new Error(`Foreign key arity mismatch in ${name}.`);
        }
        const targetName = `${foreign[2] ?? "public"}.${foreign[3]}`;
        const targets = new Set(tableRows(database.tables, targetName)
            .map((row) => JSON.stringify(keyValues(row, targetColumns))));
        for (const row of rows) {
            const values = keyValues(row, sourceColumns);
            // PostgreSQL MATCH SIMPLE exempts any composite key containing NULL.
            if (!values.includes(null) && !targets.has(JSON.stringify(values))) {
                throw new Error(`Orphaned migration reference: ${name} -> ${targetName}.`);
            }
        }
        foreignKeys++;
    }
    return { foreignKeys, uniqueKeys };
}

const activeUploads = new Set(["created", "uploading", "uploaded", "leased"]);
const activeSubmissions = new Set(["submitted", "in_review", "changes_requested", "approved_pending_signature"]);

const conditionalKeys = [
    ["profiles", ["handle"], (row) => row.handle !== null, true],
    ["publishers", ["slug"], () => true, true],
    ["organizations", ["slug"], (row) => row.deleted_at === null, true],
    ["marketplace_offers", ["product_id"], (row) => row.active === true],
    ["marketplace_reviews", ["product_id", "author_user_id"], (row) => row.author_user_id !== null && row.owner_organization_id === null],
    ["marketplace_reviews", ["product_id", "owner_organization_id"], (row) => row.owner_organization_id !== null],
    ["marketplace_entitlements", ["product_id", "user_id"], (row) => row.user_id !== null],
    ["marketplace_entitlements", ["product_id", "organization_id"], (row) => row.organization_id !== null],
    ["marketplace_uploads", ["version_id"], (row) => activeUploads.has(row.state)],
    ["marketplace_submissions", ["version_id"], (row) => activeSubmissions.has(row.state)],
];

export function verifyMarketplaceConditionalKeys(database) {
    for (const [table, names, predicate, lowercase] of conditionalKeys) {
        const name = `public.${table}`;
        assertUnique(tableRows(database.tables, name), names, false, name, predicate, lowercase);
    }
    return { conditionalUniqueKeys: conditionalKeys.length };
}
