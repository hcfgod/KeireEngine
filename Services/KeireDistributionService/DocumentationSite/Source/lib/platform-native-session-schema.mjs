const uuid = "CHAR(36) CHARACTER SET ascii COLLATE ascii_bin";
const hash = "CHAR(64) CHARACTER SET ascii COLLATE ascii_bin";

export const nativeSessionSchema = [
    `CREATE TABLE platform_native_sessions (
        id ${uuid} PRIMARY KEY,
        user_id ${uuid} NOT NULL,
        client_id VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        scope VARCHAR(128) CHARACTER SET ascii NOT NULL,
        nonce VARCHAR(128) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        account_binding ${hash} NOT NULL,
        assurance TINYINT UNSIGNED NOT NULL,
        authenticated_ms BIGINT UNSIGNED NOT NULL,
        expires_ms BIGINT UNSIGNED NOT NULL,
        revoked_ms BIGINT UNSIGNED NULL,
        CHECK (assurance IN (1,2)),
        CHECK (expires_ms=authenticated_ms+2592000000),
        INDEX native_sessions_user(user_id),
        INDEX native_sessions_expiry(expires_ms)
    ) ENGINE=InnoDB`,
    `CREATE TABLE platform_native_access_tokens (
        token_hash ${hash} PRIMARY KEY,
        session_id ${uuid} NOT NULL,
        issued_ms BIGINT UNSIGNED NOT NULL,
        expires_ms BIGINT UNSIGNED NOT NULL,
        CHECK (expires_ms>issued_ms AND expires_ms<=issued_ms+900000),
        INDEX native_access_session(session_id),
        INDEX native_access_expiry(expires_ms)
    ) ENGINE=InnoDB`,
    `CREATE TABLE platform_native_refresh_tokens (
        token_hash ${hash} PRIMARY KEY,
        session_id ${uuid} NOT NULL,
        issued_ms BIGINT UNSIGNED NOT NULL,
        expires_ms BIGINT UNSIGNED NOT NULL,
        used_ms BIGINT UNSIGNED NULL,
        CHECK (expires_ms>issued_ms),
        INDEX native_refresh_session(session_id),
        INDEX native_refresh_expiry(expires_ms)
    ) ENGINE=InnoDB`,
];
