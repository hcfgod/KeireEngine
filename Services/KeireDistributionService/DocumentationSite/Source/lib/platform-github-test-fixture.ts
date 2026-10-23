import { vi } from "vitest";
import { tokenDigest } from "./platform-auth-crypto.mjs";

export function githubFixture() {
    const records = new Map();
    const account = { id: "11111111-1111-4111-8111-111111111111", email: "fixture@example.invalid",
        email_confirmed_at: "2026-01-01 00:00:00", password_hash: null };
    const identities = [{ user_id: account.id, provider_id: "12345" }];
    const factors = [];
    const sessionToken = "t".repeat(43);
    let session = { userId: account.id, assurance: 2 };
    let now = 1700000000000;
    const execute = vi.fn(async (sql, values) => {
        if (sql.startsWith("INSERT INTO platform_github_authorizations")) {
            records.set(values[0], { state_hash: values[0], browser_hash: values[1], payload_sealed: Buffer.from(values[2]),
                expires_ms: values[3], consumed: false });
            return [{ affectedRows: 1 }];
        }
        if (sql.startsWith("SELECT * FROM platform_github_authorizations")) {
            return [[records.get(values[0])].filter(Boolean)];
        }
        if (sql.startsWith("UPDATE platform_github_authorizations")) {
            records.get(values[0]).consumed = true;
            return [{ affectedRows: 1 }];
        }
        if (sql.includes("SELECT") && sql.includes("FROM platform_identities")) {
            return [identities.filter((identity) => identity.provider_id === values[0] || identity.user_id === values[1])];
        }
        if (sql.startsWith("SELECT * FROM platform_accounts")) {
            return [[account]];
        }
        if (sql.includes("SELECT") && sql.includes("FROM platform_mfa_factors")) {
            return [factors];
        }
        if (sql.startsWith("INSERT INTO platform_identities")) {
            identities.push({ user_id: values[1], provider_id: values[2] });
            return [{ affectedRows: 1 }];
        }
        if (sql.startsWith("INSERT INTO platform_audit_events")) {
            return [{ affectedRows: 1 }];
        }
        if (sql.startsWith("INSERT INTO platform_auth_challenges")) {
            return [{ affectedRows: 1 }];
        }
        throw new Error("Unexpected SQL in GitHub fixture");
    });
    const connection = { execute, beginTransaction: vi.fn(), commit: vi.fn(), rollback: vi.fn(), release: vi.fn(), destroy: vi.fn() };
    const pool = { execute, getConnection: async () => connection };
    const auth = { pool, encryptionKey: Buffer.alloc(32, 6), now: () => now, reserveAttempt: vi.fn(async () => true),
        session: vi.fn(async (token) => token === sessionToken ? session : null),
        withSessionTransaction: vi.fn(async (token, action) => token === sessionToken && session ? action(connection, session) : null),
        issueSession: vi.fn(async () => ({ state: "authenticated", token: "n".repeat(43), userId: account.id, expiresAt: now + 28800000 })) };
    const provider = { siteOrigin: "https://platform.invalid", clientId: "fixtureclient123",
        authorize: (state) => `https://github.com/login/oauth/authorize?state=${state}`,
        identity: vi.fn(async () => ({ subject: "12345", login: "fixture", email: account.email })) };
    return { auth, provider, records, account, identities, factors, sessionToken, execute, connection,
        link: { expiresAt: now + 600000, userId: account.id, sessionHash: tokenDigest(sessionToken), next: "/account/" },
        advance: (milliseconds) => { now += milliseconds; }, expireSession: () => { session = null; }, weakenSession: () => { session.assurance = 1; } };
}
