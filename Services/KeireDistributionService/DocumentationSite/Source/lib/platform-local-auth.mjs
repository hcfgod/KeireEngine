import { createHash, randomBytes } from "node:crypto";
import bcrypt from "bcryptjs";
import { platformTransaction } from "./platform-mysql.mjs";
import { openAccountRecord } from "./platform-account-import.mjs";
import { matchingTotpCounter, newOpaqueToken, tokenDigest, verifyImportedPassword } from "./platform-auth-crypto.mjs";

const challengeLifetimeMs = 5 * 60 * 1000;
const sessionLifetimeMs = 8 * 60 * 60 * 1000;
const attemptWindowMs = 15 * 60 * 1000;
const maximumChallengeAttempts = 5;
const failed = Object.freeze({ state: "invalid_credentials" });
const digest = (value) => createHash("sha256").update(value).digest("hex");

export const authenticationSchema = [
    `CREATE TABLE platform_auth_limits (
        subject_hash CHAR(64) CHARACTER SET ascii PRIMARY KEY,
        attempts INT UNSIGNED NOT NULL,
        window_start BIGINT UNSIGNED NOT NULL
    ) ENGINE=InnoDB`,
    `CREATE TABLE platform_auth_challenges (
        token_hash CHAR(64) CHARACTER SET ascii PRIMARY KEY,
        user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        password_digest CHAR(64) CHARACTER SET ascii NOT NULL,
        attempts INT UNSIGNED NOT NULL DEFAULT 0,
        expires_ms BIGINT UNSIGNED NOT NULL,
        used BOOLEAN NOT NULL DEFAULT FALSE,
        FOREIGN KEY (user_id) REFERENCES platform_accounts(id),
        INDEX challenge_expiry (expires_ms)
    ) ENGINE=InnoDB`,
    `CREATE TABLE platform_auth_sessions (
        token_hash CHAR(64) CHARACTER SET ascii PRIMARY KEY,
        user_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
        password_digest CHAR(64) CHARACTER SET ascii NOT NULL,
        assurance_level SMALLINT NOT NULL,
        factor_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
        expires_ms BIGINT UNSIGNED NOT NULL,
        FOREIGN KEY (user_id) REFERENCES platform_accounts(id),
        FOREIGN KEY (factor_id) REFERENCES platform_mfa_factors(id),
        INDEX session_expiry (expires_ms),
        CONSTRAINT session_assurance CHECK (assurance_level IN (1, 2))
    ) ENGINE=InnoDB`,
    `CREATE TABLE platform_mfa_counters (
        factor_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
        accepted_counter BIGINT UNSIGNED NOT NULL,
        FOREIGN KEY (factor_id) REFERENCES platform_mfa_factors(id)
    ) ENGINE=InnoDB`,
];

export async function createAuthenticationSchema(pool) {
    const [rows] = await pool.query(`SELECT COUNT(*) AS count FROM information_schema.tables
        WHERE table_schema = DATABASE() AND table_name IN
        ('platform_auth_limits','platform_auth_challenges','platform_auth_sessions','platform_mfa_counters')`);
    if (Number(rows[0].count) !== 0) {
        throw new Error("Authentication tables already exist; inspect migrations before retrying.");
    }
    for (const statement of authenticationSchema) {
        await pool.query(statement);
    }
}

export function accountAvailable(account, now) {
    if (!account || account.deleted_at || !account.email_confirmed_at) {
        return false;
    }
    if (account.banned_until) {
        const bannedUntil = Date.parse(account.banned_until.replace(" ", "T") + "Z");
        if (!Number.isFinite(bannedUntil) || bannedUntil > now) {
            return false;
        }
    }
    return true;
}

export class LocalPasswordAuthentication {
    static async create(pool, encryptionKey, clock = Date.now) {
        if (!Buffer.isBuffer(encryptionKey) || encryptionKey.length !== 32) {
            throw new Error("Local authentication requires the protected account encryption key.");
        }
        const dummyHash = await bcrypt.hash(randomBytes(32).toString("base64url"), 12);
        return new LocalPasswordAuthentication(pool, Buffer.from(encryptionKey), dummyHash, clock);
    }

    constructor(pool, encryptionKey, dummyHash, clock) {
        this.pool = pool;
        this.encryptionKey = encryptionKey;
        this.dummyHash = dummyHash;
        this.clock = clock;
    }

    close() {
        this.encryptionKey.fill(0);
        this.closed = true;
    }

    now() {
        if (this.closed) {
            throw new Error("Authentication service is closed.");
        }
        const now = this.clock();
        if (!Number.isSafeInteger(now) || now < 0) {
            throw new Error("Authentication clock returned an invalid timestamp.");
        }
        return now;
    }

    async reserveAttempt(email, clientIdentity, now) {
        const limits = [
            { key: digest(`email:${email}`), maximum: 20 },
            { key: digest(`client:${clientIdentity}`), maximum: 100 },
            { key: digest(`pair:${clientIdentity}\0${email}`), maximum: 10 },
        ].sort((left, right) => left.key.localeCompare(right.key));
        return platformTransaction(this.pool, async (connection) => {
            const pending = [];
            for (const limit of limits) {
                await connection.execute(`INSERT INTO platform_auth_limits
                    (subject_hash, attempts, window_start) VALUES (?, 0, ?)
                    ON DUPLICATE KEY UPDATE subject_hash = subject_hash`, [limit.key, now]);
                const [rows] = await connection.execute(
                    "SELECT attempts, window_start FROM platform_auth_limits WHERE subject_hash = ? FOR UPDATE", [limit.key]);
                const expired = now - Number(rows[0].window_start) >= attemptWindowMs;
                const attempts = expired ? 0 : Number(rows[0].attempts);
                if (attempts >= limit.maximum) {
                    return false;
                }
                pending.push({ key: limit.key, attempts: attempts + 1, start: expired ? now : Number(rows[0].window_start) });
            }
            for (const limit of pending) {
                await connection.execute("UPDATE platform_auth_limits SET attempts = ?, window_start = ? WHERE subject_hash = ?",
                    [limit.attempts, limit.start, limit.key]);
            }
            return true;
        });
    }

    async issueSession(connection, account, now, factorId = null) {
        const token = newOpaqueToken();
        const expiresAt = now + sessionLifetimeMs;
        const assurance = factorId ? 2 : 1;
        await connection.execute(`INSERT INTO platform_auth_sessions
            (token_hash,user_id,password_digest,assurance_level,factor_id,expires_ms) VALUES (?,?,?,?,?,?)`,
        [tokenDigest(token), account.id, digest(account.password_hash ?? ""), assurance, factorId, expiresAt]);
        return { state: "authenticated", token, userId: account.id, assurance, expiresAt };
    }

    /** clientIdentity must come from trusted server/proxy metadata, never a submitted login field. */
    async signIn(email, password, clientIdentity) {
        const now = this.now();
        if (typeof email !== "string" || email.length > 320 || typeof clientIdentity !== "string" ||
            clientIdentity.length === 0 || clientIdentity.length > 256 || typeof password !== "string" || password.length > 1024) {
            return failed;
        }
        const emailKey = email.trim().toLowerCase();
        if (!await this.reserveAttempt(emailKey, clientIdentity, now)) {
            return { state: "rate_limited" };
        }
        const [accounts] = await this.pool.execute("SELECT * FROM platform_accounts WHERE email_key = ?", [emailKey]);
        const candidate = accounts[0];
        const valid = await verifyImportedPassword(password, candidate?.password_hash ?? this.dummyHash);
        if (!valid || !accountAvailable(candidate, now)) {
            return failed;
        }
        return platformTransaction(this.pool, async (connection) => {
            const [rows] = await connection.execute("SELECT * FROM platform_accounts WHERE id = ? FOR UPDATE", [candidate.id]);
            const account = rows[0];
            if (!accountAvailable(account, now) || account.password_hash !== candidate.password_hash) {
                return failed;
            }
            const [factors] = await connection.execute(
                "SELECT id, factor_type, friendly_name FROM platform_mfa_factors WHERE user_id = ? AND status = 'verified'", [account.id]);
            if (factors.length === 0) {
                return this.issueSession(connection, account, now);
            }
            if (factors.some((factor) => !["totp", "email"].includes(factor.factor_type))) {
                return { state: "unsupported_mfa" };
            }
            const challengeToken = newOpaqueToken();
            await connection.execute(`INSERT INTO platform_auth_challenges
                (token_hash,user_id,password_digest,expires_ms) VALUES (?,?,?,?)`,
            [tokenDigest(challengeToken), account.id, digest(account.password_hash), now + challengeLifetimeMs]);
            return { state: "mfa_required", challengeToken, factors };
        });
    }

    async pendingChallenge(challengeToken) {
        const now = this.now();
        const hash = tokenDigest(challengeToken);
        if (!hash) {
            return null;
        }
        const [challenges] = await this.pool.execute(`SELECT user_id,password_digest,expires_ms
            FROM platform_auth_challenges WHERE token_hash = ? AND used = FALSE AND expires_ms > ? AND attempts < ?`,
        [hash, now, maximumChallengeAttempts]);
        const challenge = challenges[0];
        if (!challenge) {
            return null;
        }
        const [accounts] = await this.pool.execute("SELECT * FROM platform_accounts WHERE id = ?", [challenge.user_id]);
        const account = accounts[0];
        if (!accountAvailable(account, now) || digest(account.password_hash ?? "") !== challenge.password_digest) {
            return null;
        }
        const [factors] = await this.pool.execute(`SELECT id,factor_type,friendly_name FROM platform_mfa_factors
            WHERE user_id = ? AND status = 'verified' ORDER BY id`, [account.id]);
        if (factors.length === 0 || factors.some((factor) => !["totp", "email"].includes(factor.factor_type))) {
            return null;
        }
        // Display metadata only. Completion rechecks all mutable state under transaction locks.
        return { factors, expiresAt: Number(challenge.expires_ms) };
    }

    async completeTotp(challengeToken, factorId, code) {
        const now = this.now();
        const challengeHash = tokenDigest(challengeToken);
        if (!challengeHash || typeof factorId !== "string" || factorId.length !== 36) {
            return failed;
        }
        return platformTransaction(this.pool, async (connection) => {
            const [challenges] = await connection.execute(
                "SELECT * FROM platform_auth_challenges WHERE token_hash = ? FOR UPDATE", [challengeHash]);
            const challenge = challenges[0];
            if (!challenge || challenge.used || Number(challenge.expires_ms) <= now || challenge.attempts >= maximumChallengeAttempts) {
                return failed;
            }
            const [accounts] = await connection.execute("SELECT * FROM platform_accounts WHERE id = ? FOR UPDATE", [challenge.user_id]);
            const account = accounts[0];
            if (!accountAvailable(account, now) || digest(account.password_hash ?? "") !== challenge.password_digest) {
                return failed;
            }
            await connection.execute("UPDATE platform_auth_challenges SET attempts = attempts + 1 WHERE token_hash = ?", [challengeHash]);
            const [factors] = await connection.execute(`SELECT * FROM platform_mfa_factors
                WHERE id = ? AND user_id = ? AND status = 'verified' AND factor_type = 'totp' FOR UPDATE`, [factorId, account.id]);
            if (factors.length === 0) {
                return failed;
            }
            const factor = openAccountRecord(factors[0].source_sealed, this.encryptionKey, `mfa:${factorId}`);
            const [counters] = await connection.execute("SELECT accepted_counter FROM platform_mfa_counters WHERE factor_id = ? FOR UPDATE", [factorId]);
            const counter = matchingTotpCounter(factor.secret, code, now,
                counters.length ? Number(counters[0].accepted_counter) : null);
            if (counter === null) {
                return failed;
            }
            await connection.execute(`INSERT INTO platform_mfa_counters (factor_id,accepted_counter) VALUES (?,?)
                ON DUPLICATE KEY UPDATE accepted_counter = ?`, [factorId, counter, counter]);
            await connection.execute("UPDATE platform_auth_challenges SET used = TRUE WHERE token_hash = ?", [challengeHash]);
            return this.issueSession(connection, account, now, factorId);
        });
    }

    async session(token) {
        const now = this.now();
        const hash = tokenDigest(token);
        if (!hash) {
            return null;
        }
        const [sessions] = await this.pool.execute("SELECT * FROM platform_auth_sessions WHERE token_hash = ? AND expires_ms > ?", [hash, now]);
        const session = sessions[0];
        if (!session) {
            return null;
        }
        const [accounts] = await this.pool.execute("SELECT * FROM platform_accounts WHERE id = ?", [session.user_id]);
        const account = accounts[0];
        if (!accountAvailable(account, now) || digest(account.password_hash ?? "") !== session.password_digest) {
            return null;
        }
        const [factors] = await this.pool.execute("SELECT * FROM platform_mfa_factors WHERE user_id = ? AND status = 'verified'", [account.id]);
        const matchedFactor = factors.find((factor) => factor.id === session.factor_id);
        if (matchedFactor?.factor_type === "email") {
            const enrollment = openAccountRecord(matchedFactor.source_sealed, this.encryptionKey, `mfa:${matchedFactor.id}`);
            if (session.assurance_level !== 1 || enrollment.email !== account.email) {
                return null;
            }
            // Email verification is a normal sign-in method, not an upgrade for privileged AAL2 operations.
            return { userId: account.id, assurance: 1, expiresAt: Number(session.expires_ms), method: "email" };
        }
        if ((factors.length > 0 && session.assurance_level < 2) ||
            (session.assurance_level === 2 && matchedFactor?.factor_type !== "totp") ||
            (session.factor_id && !matchedFactor)) {
            return null;
        }
        return { userId: account.id, assurance: session.assurance_level, expiresAt: Number(session.expires_ms) };
    }

    async signOut(token) {
        this.now();
        const hash = tokenDigest(token);
        if (hash) {
            await this.pool.execute("DELETE FROM platform_auth_sessions WHERE token_hash = ?", [hash]);
        }
    }
}
