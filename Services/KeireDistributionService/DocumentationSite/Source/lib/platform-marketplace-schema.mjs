import { marketplaceAccessTables } from "./platform-marketplace-access-schema.mjs";

const uuid = "CHAR(36) CHARACTER SET ascii COLLATE ascii_bin";
const time = "DATETIME(6)";
const semver = "^(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$";
const productStates = "'draft','validation','submitted','changes_requested','approved_pending_signature','published','delisted','suspended'";
const versionStates = "'draft','uploaded','validating','validation_failed','validated','submitted','changes_requested','approved_pending_signature','published','withdrawn','security_revoked'";

// Ordered by dependency. Column definitions are reviewed code, never supplied by a request or source export.
export const marketplaceCoreTables = [
    {
        name: "organizations",
        columns: {
            id: `${uuid} NOT NULL`, slug: "VARCHAR(63) NOT NULL", display_name: "VARCHAR(96) NOT NULL",
            avatar_url: "VARCHAR(2048) NULL", created_by: `${uuid} NOT NULL`, created_at: `${time} NOT NULL`,
            updated_at: `${time} NOT NULL`, deleted_at: `${time} NULL`,
        },
        extra: [
            "PRIMARY KEY (id)",
            "active_slug VARCHAR(63) GENERATED ALWAYS AS (CASE WHEN deleted_at IS NULL THEN LOWER(slug) ELSE NULL END) STORED",
            "UNIQUE KEY organization_active_slug (active_slug)",
            "FOREIGN KEY (created_by) REFERENCES platform_accounts(id)",
            "CHECK (REGEXP_LIKE(slug, '^[a-z0-9][a-z0-9-]{2,62}$', 'c'))",
            "CHECK (CHAR_LENGTH(display_name) BETWEEN 1 AND 96)",
            "CHECK (avatar_url IS NULL OR (CHAR_LENGTH(avatar_url) <= 2048 AND avatar_url LIKE 'https://%'))",
        ],
    },
    {
        name: "organization_memberships",
        columns: {
            organization_id: `${uuid} NOT NULL`, user_id: `${uuid} NOT NULL`, role: "VARCHAR(16) NOT NULL",
            invited_by: `${uuid} NULL`, created_at: `${time} NOT NULL`,
        },
        extra: [
            "PRIMARY KEY (organization_id, user_id)",
            "FOREIGN KEY (organization_id) REFERENCES organizations(id) ON DELETE CASCADE",
            "FOREIGN KEY (user_id) REFERENCES platform_accounts(id) ON DELETE CASCADE",
            "FOREIGN KEY (invited_by) REFERENCES platform_accounts(id) ON DELETE SET NULL",
            "CHECK (role IN ('owner','admin','member'))",
        ],
    },
    {
        name: "publishers",
        columns: {
            id: `${uuid} NOT NULL`, organization_id: `${uuid} NOT NULL`, slug: "VARCHAR(63) NOT NULL",
            display_name: "VARCHAR(96) NOT NULL", summary: "TEXT NOT NULL", website_url: "VARCHAR(2048) NULL",
            verified: "BOOLEAN NOT NULL", suspended_at: `${time} NULL`, created_at: `${time} NOT NULL`,
            updated_at: `${time} NOT NULL`,
        },
        extra: [
            "PRIMARY KEY (id)", "UNIQUE KEY publisher_organization (organization_id)",
            "slug_key VARCHAR(63) GENERATED ALWAYS AS (LOWER(slug)) STORED", "UNIQUE KEY publisher_slug (slug_key)",
            "FOREIGN KEY (organization_id) REFERENCES organizations(id)",
            "CHECK (REGEXP_LIKE(slug, '^[a-z0-9][a-z0-9-]{2,62}$', 'c'))",
            "CHECK (CHAR_LENGTH(display_name) BETWEEN 2 AND 96)", "CHECK (CHAR_LENGTH(summary) <= 1000)",
            "CHECK (website_url IS NULL OR (CHAR_LENGTH(website_url) <= 2048 AND website_url LIKE 'https://%'))",
            "CHECK (verified IN (0,1))",
        ],
    },
    {
        name: "marketplace_categories",
        columns: {
            id: `${uuid} NOT NULL`, slug: "VARCHAR(63) NOT NULL", display_name: "VARCHAR(64) NOT NULL",
            description: "TEXT NOT NULL", sort_order: "INT NOT NULL", active: "BOOLEAN NOT NULL",
        },
        extra: [
            "PRIMARY KEY (id)", "UNIQUE KEY category_slug (slug)",
            "CHECK (REGEXP_LIKE(slug, '^[a-z0-9][a-z0-9-]{1,62}$', 'c'))",
            "CHECK (CHAR_LENGTH(display_name) BETWEEN 2 AND 64)", "CHECK (CHAR_LENGTH(description) <= 500)",
            "CHECK (active IN (0,1))",
        ],
    },
    {
        name: "marketplace_tags",
        columns: { id: `${uuid} NOT NULL`, slug: "VARCHAR(63) NOT NULL", display_name: "VARCHAR(64) NOT NULL" },
        extra: [
            "PRIMARY KEY (id)", "UNIQUE KEY tag_slug (slug)",
            "CHECK (REGEXP_LIKE(slug, '^[a-z0-9][a-z0-9-]{1,62}$', 'c'))",
            "CHECK (CHAR_LENGTH(display_name) BETWEEN 2 AND 64)",
        ],
    },
    {
        name: "marketplace_license_revisions",
        columns: {
            license_id: "VARCHAR(64) NOT NULL", revision: "VARCHAR(64) NOT NULL", display_name: "VARCHAR(128) NOT NULL",
            license_kind: "VARCHAR(32) NOT NULL", canonical_url: "VARCHAR(2048) NOT NULL",
            acceptance_snapshot: "LONGTEXT NOT NULL", approved_by: `${uuid} NULL`, approved_at: `${time} NOT NULL`,
        },
        extra: [
            "PRIMARY KEY (license_id, revision)", "FOREIGN KEY (approved_by) REFERENCES platform_accounts(id)",
            "CHECK (CHAR_LENGTH(license_id) BETWEEN 2 AND 64 AND CHAR_LENGTH(revision) BETWEEN 1 AND 64)",
            "CHECK (CHAR_LENGTH(display_name) BETWEEN 2 AND 128)",
            "CHECK (CHAR_LENGTH(acceptance_snapshot) BETWEEN 1 AND 100000)",
            "CHECK (CHAR_LENGTH(canonical_url) BETWEEN 8 AND 2048 AND canonical_url LIKE 'https://%')",
            "CHECK ((license_kind = 'spdx' AND license_id IN ('MIT','Apache-2.0','BSD-3-Clause')) OR " +
                "(license_kind = 'creative_commons' AND license_id IN ('CC0-1.0','CC-BY-4.0')) OR " +
                "(license_kind = 'custom' AND REGEXP_LIKE(license_id, '^LicenseRef-Keire-[A-Za-z0-9.-]{1,46}$', 'c') AND approved_by IS NOT NULL))",
        ],
    },
    {
        name: "marketplace_products",
        columns: {
            id: `${uuid} NOT NULL`, publisher_id: `${uuid} NOT NULL`, category_id: `${uuid} NOT NULL`,
            slug: "VARCHAR(96) NOT NULL", display_name: "VARCHAR(128) NOT NULL", short_description: "VARCHAR(240) NOT NULL",
            description_markdown: "LONGTEXT NOT NULL", state: "VARCHAR(32) NOT NULL", license_spdx: "VARCHAR(64) NOT NULL",
            license_revision: "VARCHAR(64) NOT NULL", support_url: "VARCHAR(2048) NULL", repository_url: "VARCHAR(2048) NULL",
            featured: "BOOLEAN NOT NULL", published_at: `${time} NULL`, delisted_at: `${time} NULL`,
            created_at: `${time} NOT NULL`, updated_at: `${time} NOT NULL`,
        },
        extra: [
            "PRIMARY KEY (id)", "UNIQUE KEY product_publisher_slug (publisher_id, slug)",
            "FOREIGN KEY (publisher_id) REFERENCES publishers(id)",
            "FOREIGN KEY (category_id) REFERENCES marketplace_categories(id)",
            "FOREIGN KEY (license_spdx, license_revision) REFERENCES marketplace_license_revisions(license_id, revision)",
            "CHECK (REGEXP_LIKE(slug, '^[a-z0-9][a-z0-9-]{1,95}$', 'c'))",
            "CHECK (CHAR_LENGTH(display_name) BETWEEN 2 AND 128)", "CHECK (CHAR_LENGTH(short_description) BETWEEN 20 AND 240)",
            "CHECK (CHAR_LENGTH(description_markdown) <= 100000)",
            "CHECK (CHAR_LENGTH(license_spdx) BETWEEN 2 AND 64 AND CHAR_LENGTH(license_revision) BETWEEN 1 AND 64)",
            "CHECK (support_url IS NULL OR (CHAR_LENGTH(support_url) <= 2048 AND support_url LIKE 'https://%'))",
            "CHECK (repository_url IS NULL OR (CHAR_LENGTH(repository_url) <= 2048 AND repository_url LIKE 'https://%'))",
            `CHECK (state IN (${productStates}))`, "CHECK (featured IN (0,1))",
        ],
    },
    {
        name: "marketplace_product_versions",
        columns: {
            id: `${uuid} NOT NULL`, product_id: `${uuid} NOT NULL`, version: "VARCHAR(255) NOT NULL", state: "VARCHAR(32) NOT NULL",
            install_kind: "VARCHAR(32) NOT NULL", minimum_engine_version: "LONGTEXT NOT NULL", maximum_engine_version: "LONGTEXT NULL",
            platforms: "JSON NOT NULL", architectures: "JSON NOT NULL", renderer_capabilities: "JSON NOT NULL",
            managed_api_version: "LONGTEXT NULL", release_notes_markdown: "LONGTEXT NOT NULL", archive_storage_path: "TEXT NULL",
            archive_sha256: "VARCHAR(64) NULL", archive_size_bytes: "BIGINT NULL", manifest_sha256: "VARCHAR(64) NULL",
            signature_key_id: "LONGTEXT NULL", validator_version: "LONGTEXT NULL", executable_code_fingerprint: "LONGTEXT NULL",
            published_at: `${time} NULL`, withdrawn_at: `${time} NULL`, security_revoked_at: `${time} NULL`,
            created_at: `${time} NOT NULL`, updated_at: `${time} NOT NULL`, archive_storage_bucket: "VARCHAR(64) NULL",
        },
        extra: [
            "PRIMARY KEY (id)", "UNIQUE KEY product_version (product_id, version)",
            "FOREIGN KEY (product_id) REFERENCES marketplace_products(id)",
            `CHECK (state IN (${versionStates}))`, "CHECK (install_kind IN ('registry','asset_import','complete_project'))",
            `CHECK (REGEXP_LIKE(version, '${semver}', 'c'))`,
            `CHECK (REGEXP_LIKE(minimum_engine_version, '${semver}', 'c'))`,
            `CHECK (maximum_engine_version IS NULL OR REGEXP_LIKE(maximum_engine_version, '${semver}', 'c'))`,
            "CHECK (JSON_TYPE(platforms) = 'ARRAY' AND JSON_TYPE(architectures) = 'ARRAY' AND JSON_TYPE(renderer_capabilities) = 'ARRAY')",
            "CHECK (CHAR_LENGTH(release_notes_markdown) <= 50000)",
            "CHECK ((archive_storage_path IS NULL AND archive_storage_bucket IS NULL) OR " +
                "(archive_storage_path IS NOT NULL AND archive_storage_bucket IN ('marketplace-quarantine','marketplace-releases','marketplace-packages')))",
            "CHECK ((archive_storage_path IS NULL AND archive_sha256 IS NULL AND archive_size_bytes IS NULL) OR " +
                "(CHAR_LENGTH(archive_storage_path) BETWEEN 1 AND 1024 AND NOT REGEXP_LIKE(archive_storage_path, '(^|/)[.][.](/|$)', 'c') " +
                "AND REGEXP_LIKE(archive_sha256, '^[0-9a-f]{64}$', 'c') AND archive_size_bytes BETWEEN 1 AND 68719476736))",
            "CHECK (manifest_sha256 IS NULL OR REGEXP_LIKE(manifest_sha256, '^[0-9a-f]{64}$', 'c'))",
        ],
    },
    {
        name: "marketplace_offers",
        columns: {
            id: `${uuid} NOT NULL`, product_id: `${uuid} NOT NULL`, currency: "CHAR(3) NOT NULL",
            amount_minor: "BIGINT NOT NULL", active: "BOOLEAN NOT NULL", created_at: `${time} NOT NULL`,
        },
        extra: [
            "PRIMARY KEY (id)",
            `active_product ${uuid} GENERATED ALWAYS AS (CASE WHEN active THEN product_id ELSE NULL END) VIRTUAL`,
            "UNIQUE KEY offer_active_product (active_product)",
            "FOREIGN KEY (product_id) REFERENCES marketplace_products(id) ON DELETE CASCADE",
            "CHECK (REGEXP_LIKE(currency, '^[A-Z]{3}$', 'c'))", "CHECK (amount_minor = 0)", "CHECK (active IN (0,1))",
        ],
    },
];

export function marketplaceTableSql(table) {
    if (!marketplaceCoreTables.includes(table) && !marketplaceAccessTables.includes(table)) {
        throw new Error("Only reviewed marketplace table definitions may be created.");
    }
    const fields = Object.entries(table.columns).map(([name, definition]) => `\`${name}\` ${definition}`);
    return `CREATE TABLE \`${table.name}\` (\n    ${[...fields, ...table.extra].join(",\n    ")}\n)` +
        " ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_bin";
}

export function marketplaceTables(section = "marketplace-core") {
    if (section === "marketplace-core") {
        return marketplaceCoreTables;
    }
    if (section === "marketplace-access") {
        return marketplaceAccessTables;
    }
    throw new Error("Unknown marketplace migration section.");
}

export async function createMarketplaceSchema(pool, section = "marketplace-core") {
    const tables = marketplaceTables(section);
    const names = tables.map((table) => table.name);
    const [existing] = await pool.execute(`SELECT table_name FROM information_schema.tables
        WHERE table_schema = DATABASE() AND table_name IN (${names.map(() => "?").join(",")})`, names);
    if (existing.length) {
        throw new Error("Marketplace tables already exist; inspect schema state before retrying.");
    }
    for (const table of tables) {
        await pool.query(marketplaceTableSql(table));
    }
}
