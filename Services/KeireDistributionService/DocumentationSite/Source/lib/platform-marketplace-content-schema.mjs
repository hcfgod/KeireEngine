const uuid = "CHAR(36) CHARACTER SET ascii COLLATE ascii_bin";

export const marketplaceContentTables = [
    {
        name: "marketplace_product_media",
        columns: {
            id: `${uuid} NOT NULL`,
            product_id: `${uuid} NOT NULL`,
            storage_path: "TEXT NOT NULL",
            media_type: "VARCHAR(16) NOT NULL",
            alt_text: "VARCHAR(300) NOT NULL",
            width: "INT NULL",
            height: "INT NULL",
            sort_order: "INT NOT NULL",
            approved_at: "DATETIME(6) NULL",
            sha256: "VARCHAR(64) NOT NULL",
        },
        extra: [
            "PRIMARY KEY (id)",
            "FOREIGN KEY (product_id) REFERENCES marketplace_products(id) ON DELETE CASCADE",
            "CHECK (CHAR_LENGTH(alt_text) BETWEEN 1 AND 300)",
            "CHECK ((width IS NULL AND height IS NULL) OR (width BETWEEN 1 AND 16384 AND height BETWEEN 1 AND 16384))",
            "CHECK (CHAR_LENGTH(storage_path) BETWEEN 1 AND 1024 AND NOT REGEXP_LIKE(storage_path, '(^|/)[.][.](/|$)', 'c'))",
            "CHECK (REGEXP_LIKE(sha256, '^[0-9a-f]{64}$', 'c'))",
            "CHECK (media_type IN ('image/avif','image/webp','image/png','video/mp4'))",
        ],
    },
    {
        name: "marketplace_product_tags",
        columns: {
            product_id: `${uuid} NOT NULL`,
            tag_id: `${uuid} NOT NULL`,
        },
        extra: [
            "PRIMARY KEY (product_id, tag_id)",
            "FOREIGN KEY (product_id) REFERENCES marketplace_products(id) ON DELETE CASCADE",
            "FOREIGN KEY (tag_id) REFERENCES marketplace_tags(id) ON DELETE CASCADE",
        ],
    },
    {
        name: "marketplace_version_dependencies",
        columns: {
            version_id: `${uuid} NOT NULL`,
            package_id: "VARCHAR(128) NOT NULL",
            version_range: "VARCHAR(128) NOT NULL",
            conflict: "BOOLEAN NOT NULL",
        },
        extra: [
            "PRIMARY KEY (version_id, package_id, conflict)",
            "FOREIGN KEY (version_id) REFERENCES marketplace_product_versions(id) ON DELETE CASCADE",
            "CHECK (REGEXP_LIKE(package_id, '^[a-z0-9][a-z0-9.-]{2,127}$', 'c') AND package_id LIKE '%.%')",
            "CHECK (CHAR_LENGTH(version_range) BETWEEN 1 AND 128)",
            "CHECK (conflict IN (0,1))",
        ],
    },
    {
        name: "marketplace_wishlist_items",
        columns: {
            user_id: `${uuid} NOT NULL`,
            product_id: `${uuid} NOT NULL`,
            created_at: "DATETIME(6) NOT NULL",
        },
        extra: [
            "PRIMARY KEY (user_id, product_id)",
            "FOREIGN KEY (user_id) REFERENCES platform_accounts(id) ON DELETE CASCADE",
            "FOREIGN KEY (product_id) REFERENCES marketplace_products(id) ON DELETE CASCADE",
        ],
    },
];
