import { createCipheriv, createDecipheriv, createHash, randomBytes } from "node:crypto";
import { platformTransaction } from "./platform-mysql.mjs";

const uuidPattern = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/i;

export function sealAccountRecord(value, key, purpose) {
    if (!Buffer.isBuffer(key) || key.length !== 32 || typeof purpose !== "string" || purpose.length === 0) {
        throw new Error("Account encryption requires a 256-bit key and a record-specific purpose.");
    }
    const nonce = randomBytes(12);
    const cipher = createCipheriv("aes-256-gcm", key, nonce);
    cipher.setAAD(Buffer.from(purpose));
    const plaintext = Buffer.from(JSON.stringify(value));
    try {
        const ciphertext = Buffer.concat([cipher.update(plaintext), cipher.final()]);
        return Buffer.concat([Buffer.from([1]), nonce, cipher.getAuthTag(), ciphertext]);
    } finally {
        plaintext.fill(0);
    }
}

export function openAccountRecord(envelope, key, purpose) {
    if (!Buffer.isBuffer(envelope) || envelope.length < 30 || envelope[0] !== 1 ||
        !Buffer.isBuffer(key) || key.length !== 32 || typeof purpose !== "string" || purpose.length === 0) {
        throw new Error("Invalid encrypted account record.");
    }
    const decipher = createDecipheriv("aes-256-gcm", key, envelope.subarray(1, 13));
    decipher.setAAD(Buffer.from(purpose));
    decipher.setAuthTag(envelope.subarray(13, 29));
    const plaintext = Buffer.concat([decipher.update(envelope.subarray(29)), decipher.final()]);
    try {
        return JSON.parse(plaintext.toString("utf8"));
    } finally {
        plaintext.fill(0);
    }
}

export function mysqlUtcTimestamp(value) {
    if (value === null || value === undefined || value === "") {
        return null;
    }
    const match = typeof value === "string" && value.match(
        /^(\d{4}-\d{2}-\d{2})T(\d{2}:\d{2}:\d{2})(?:\.(\d{1,6}))?(Z|[+-]\d{2}:\d{2})$/,
    );
    if (!match || Number(match[1].slice(0, 4)) < 1000) {
        throw new Error("Unsupported account timestamp; import stopped rather than changing it.");
    }
    const calendarDate = new Date(`${match[1]}T${match[2]}Z`);
    if (!Number.isFinite(calendarDate.getTime()) ||
        calendarDate.toISOString().slice(0, 19) !== `${match[1]}T${match[2]}`) {
        throw new Error("Invalid account timestamp calendar fields.");
    }
    const milliseconds = Date.parse(`${match[1]}T${match[2]}${match[4]}`);
    if (!Number.isFinite(milliseconds) || !/^\d{4}-/.test(new Date(milliseconds).toISOString())) {
        throw new Error("Invalid account timestamp.");
    }
    return new Date(milliseconds).toISOString().slice(0, 19).replace("T", " ") +
        "." + (match[3] ?? "").padEnd(6, "0");
}

export const accountSchema = [
    `CREATE TABLE platform_accounts (
        id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
        email VARCHAR(320) NULL,
        email_key VARCHAR(320) NULL UNIQUE,
        password_hash VARCHAR(255) CHARACTER SET ascii NULL,
        email_confirmed_at DATETIME(6) NULL,
        phone VARCHAR(64) NULL,
        phone_confirmed_at DATETIME(6) NULL,
        banned_until DATETIME(6) NULL,
        deleted_at DATETIME(6) NULL,
        created_at DATETIME(6) NULL,
        updated_at DATETIME(6) NULL,
        app_metadata JSON NOT NULL,
        user_metadata JSON NOT NULL,
        source_sealed LONGBLOB NOT NULL
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin`,
    `CREATE TABLE platform_identities (
        id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
        user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        provider VARCHAR(64) NOT NULL,
        provider_id VARCHAR(255) NOT NULL,
        identity_data JSON NOT NULL,
        created_at DATETIME(6) NULL,
        updated_at DATETIME(6) NULL,
        source_record JSON NOT NULL,
        UNIQUE KEY provider_subject (provider, provider_id),
        FOREIGN KEY (user_id) REFERENCES platform_accounts(id)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin`,
    `CREATE TABLE platform_mfa_factors (
        id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
        user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        factor_type VARCHAR(32) NOT NULL,
        status VARCHAR(32) NOT NULL,
        friendly_name TEXT NULL,
        source_sealed LONGBLOB NOT NULL,
        FOREIGN KEY (user_id) REFERENCES platform_accounts(id),
        CONSTRAINT mfa_status CHECK (status IN ('verified', 'unverified'))
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin`,
    `CREATE TABLE platform_profiles (
        user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
        display_name TEXT NULL,
        avatar_url TEXT NULL,
        handle VARCHAR(64) NULL UNIQUE,
        source_record JSON NOT NULL,
        FOREIGN KEY (user_id) REFERENCES platform_accounts(id)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin`,
    `CREATE TABLE platform_import_batches (
        source_digest CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        source_project VARCHAR(64) NOT NULL,
        section VARCHAR(32) NOT NULL,
        row_counts JSON NOT NULL,
        imported_at DATETIME(6) NOT NULL DEFAULT (UTC_TIMESTAMP(6)),
        PRIMARY KEY (source_digest, section)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin`,
];

export async function createInitialAccountSchema(pool) {
    const [tables] = await pool.execute(
        "SELECT COUNT(*) AS count FROM information_schema.tables WHERE table_schema = DATABASE()",
    );
    if (Number(tables[0].count) !== 0) {
        throw new Error("Initial schema creation requires an empty Kéire database; inspect partial migrations manually.");
    }
    // MySQL DDL commits independently. Never silently adopt or drop an existing table after a partial failure.
    for (const statement of accountSchema) {
        await pool.query(statement);
    }
}

export function accountImportRecords(snapshot) {
    if (snapshot?.project !== "khjduyjamzwumhducmou" || !snapshot.tables) {
        throw new Error("The account snapshot is not from the expected Kéire project.");
    }
    const records = {
        users: snapshot.tables["auth.users"],
        identities: snapshot.tables["auth.identities"],
        factors: snapshot.tables["auth.mfa_factors"],
        profiles: snapshot.tables["public.profiles"],
    };
    for (const rows of Object.values(records)) {
        if (!Array.isArray(rows)) {
            throw new Error("An account export table is missing.");
        }
    }
    const userIds = new Set();
    for (const user of records.users) {
        if (!uuidPattern.test(user.id) || userIds.has(user.id)) {
            throw new Error("Invalid or duplicate source account identity.");
        }
        userIds.add(user.id);
        if (user.encrypted_password && !/^\$2[aby]\$\d{2}\$[./A-Za-z0-9]{53}$/.test(user.encrypted_password)) {
            throw new Error("Unsupported password hash; the account must not be imported with a replacement password.");
        }
    }
    for (const row of [...records.identities, ...records.factors, ...records.profiles]) {
        if (!userIds.has(row.user_id)) {
            throw new Error("An account record references a missing source user.");
        }
    }
    for (const row of [...records.identities, ...records.factors]) {
        if (!uuidPattern.test(row.id)) {
            throw new Error("Invalid linked identity or MFA identifier.");
        }
    }
    for (const factor of records.factors) {
        if (factor.factor_type !== "totp" || !["verified", "unverified"].includes(factor.status) ||
            (factor.status === "verified" && !/^[A-Z2-7]{16,128}$/.test(factor.secret))) {
            throw new Error("Unsupported MFA factor; do not disable or discard it during migration.");
        }
    }
    return records;
}

export async function importAccountSnapshot(pool, rawSnapshot, expectedDigest, encryptionKey) {
    if (!Buffer.isBuffer(encryptionKey) || encryptionKey.length !== 32) {
        throw new Error("Account import requires its protected 256-bit encryption key.");
    }
    if (!/^[a-f0-9]{64}$/.test(expectedDigest) ||
        createHash("sha256").update(rawSnapshot).digest("hex") !== expectedDigest) {
        throw new Error("The account import does not match the verified snapshot digest.");
    }
    const snapshot = JSON.parse(rawSnapshot);
    const records = accountImportRecords(snapshot);
    const counts = Object.fromEntries(Object.entries(records).map(([name, rows]) => [name, rows.length]));
    return platformTransaction(pool, async (connection) => {
        for (const user of records.users) {
            await connection.execute(`INSERT INTO platform_accounts
                (id, email, email_key, password_hash, email_confirmed_at, phone, phone_confirmed_at,
                 banned_until, deleted_at, created_at, updated_at, app_metadata, user_metadata, source_sealed)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`, [
                user.id, user.email || null, user.email ? user.email.trim().toLowerCase() : null,
                user.encrypted_password || null, mysqlUtcTimestamp(user.email_confirmed_at), user.phone || null,
                mysqlUtcTimestamp(user.phone_confirmed_at), mysqlUtcTimestamp(user.banned_until),
                mysqlUtcTimestamp(user.deleted_at), mysqlUtcTimestamp(user.created_at), mysqlUtcTimestamp(user.updated_at),
                JSON.stringify(user.raw_app_meta_data ?? {}), JSON.stringify(user.raw_user_meta_data ?? {}),
                sealAccountRecord(user, encryptionKey, `account:${user.id}`),
            ]);
        }
        for (const identity of records.identities) {
            await connection.execute(`INSERT INTO platform_identities
                (id, user_id, provider, provider_id, identity_data, created_at, updated_at, source_record)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)`, [
                identity.id, identity.user_id, identity.provider, identity.provider_id,
                JSON.stringify(identity.identity_data ?? {}), mysqlUtcTimestamp(identity.created_at),
                mysqlUtcTimestamp(identity.updated_at), JSON.stringify(identity),
            ]);
        }
        for (const factor of records.factors) {
            await connection.execute(`INSERT INTO platform_mfa_factors
                (id, user_id, factor_type, status, friendly_name, source_sealed) VALUES (?, ?, ?, ?, ?, ?)`, [
                factor.id, factor.user_id, factor.factor_type, factor.status, factor.friendly_name ?? null,
                sealAccountRecord(factor, encryptionKey, `mfa:${factor.id}`),
            ]);
        }
        for (const profile of records.profiles) {
            await connection.execute(`INSERT INTO platform_profiles
                (user_id, display_name, avatar_url, handle, source_record) VALUES (?, ?, ?, ?, ?)`, [
                profile.user_id, profile.display_name, profile.avatar_url, profile.handle, JSON.stringify(profile),
            ]);
        }
        await connection.execute(`INSERT INTO platform_import_batches
            (source_digest, source_project, section, row_counts) VALUES (?, ?, 'accounts', ?)`,
        [expectedDigest, snapshot.project, JSON.stringify(counts)]);
        return counts;
    });
}
