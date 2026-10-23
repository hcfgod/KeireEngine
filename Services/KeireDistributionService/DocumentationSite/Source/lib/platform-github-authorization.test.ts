import { describe, expect, it } from "vitest";
import { beginGitHubAuthorization, consumeGitHubAuthorization, completeGitHubIdentity, githubNextPath }
    from "./platform-github-authorization.mjs";
import { githubFixture } from "./platform-github-test-fixture";

const identity = { subject: "12345", login: "fixture", email: "fixture@example.invalid" };
async function start(fixture, token = null) {
    const result = await beginGitHubAuthorization(fixture.auth, fixture.provider, token, "/account/", "loopback");
    return { browser: result.browser, state: new URL(result.url).searchParams.get("state") };
}

describe("GitHub local authorization", () => {
    it("stores verifier encrypted, binds browser, and consumes state once", async () => {
        const f = githubFixture();
        const flow = await start(f);
        expect(await consumeGitHubAuthorization(f.auth, f.provider, flow.state, "x".repeat(43), null)).toBeNull();
        const payload = await consumeGitHubAuthorization(f.auth, f.provider, flow.state, flow.browser, null);
        expect(payload.verifier).toHaveLength(43);
        expect([...f.records.values()][0].payload_sealed.includes(payload.verifier)).toBe(false);
        expect(await consumeGitHubAuthorization(f.auth, f.provider, flow.state, flow.browser, null)).toBeNull();
    });

    it("rejects expired state and changed provider or session binding", async () => {
        for (const change of ["expired", "origin", "client", "session"]) {
            const f = githubFixture();
            const flow = await start(f, f.sessionToken);
            if (change === "expired") {
                f.advance(600000);
            }
            if (change === "origin") {
                f.provider.siteOrigin = "https://other.invalid";
            }
            if (change === "client") {
                f.provider.clientId = "differentclient";
            }
            expect(await consumeGitHubAuthorization(f.auth, f.provider, flow.state, flow.browser,
                change === "session" ? null : f.sessionToken)).toBeNull();
        }
    });

    it("rate limits authorization before allocating state", async () => {
        const f = githubFixture();
        f.auth.reserveAttempt.mockResolvedValue(false);
        expect(await beginGitHubAuthorization(f.auth, f.provider, null, "/account/", "loopback"))
            .toEqual({ state: "rate_limited" });
        expect(f.records.size).toBe(0);
    });

    it("signs into an existing immutable provider subject, never by matching email", async () => {
        const f = githubFixture();
        expect((await completeGitHubIdentity(f.auth, identity, { userId: null, expiresAt: f.auth.now() + 600000 }, null)).state).toBe("authenticated");
        f.identities.length = 0;
        expect((await completeGitHubIdentity(f.auth, identity, { userId: null, expiresAt: f.auth.now() + 600000 }, null)).state).toBe("registration_required");
        expect(f.auth.issueSession).toHaveBeenCalledTimes(1);
    });

    it("preserves enrolled MFA including accounts without a password", async () => {
        const f = githubFixture();
        f.factors.push({ id: "factor", factor_type: "totp" });
        const result = await completeGitHubIdentity(f.auth, identity, { userId: null, expiresAt: f.auth.now() + 600000 }, null);
        expect(result.state).toBe("mfa_required");
        expect(result.challengeToken).toHaveLength(43);
        expect(f.auth.issueSession).not.toHaveBeenCalled();
        f.factors[0].factor_type = "unknown";
        expect((await completeGitHubIdentity(f.auth, identity, { userId: null, expiresAt: f.auth.now() + 600000 }, null)).state).toBe("unsupported_mfa");
    });

    it("refuses banned or deleted accounts", async () => {
        const f = githubFixture();
        f.account.deleted_at = "2026-01-01 00:00:00";
        expect((await completeGitHubIdentity(f.auth, identity, { userId: null, expiresAt: f.auth.now() + 600000 }, null)).state).toBe("invalid_identity");
        expect(f.auth.issueSession).not.toHaveBeenCalled();
    });

    it("links only to the still-valid starting session and makes repeat linking idempotent", async () => {
        const f = githubFixture();
        f.identities.length = 0;
        expect((await completeGitHubIdentity(f.auth, identity, f.link, f.sessionToken)).state).toBe("linked");
        expect((await completeGitHubIdentity(f.auth, identity, f.link, f.sessionToken)).state).toBe("linked");
        expect(f.identities).toHaveLength(1);
        f.expireSession();
        expect((await completeGitHubIdentity(f.auth, identity, f.link, f.sessionToken)).state).toBe("invalid_session");
    });

    it("never transfers another account's identity or replaces a different linked GitHub identity", async () => {
        for (const conflict of ["owner", "subject"]) {
            const f = githubFixture();
            if (conflict === "owner") {
                f.identities[0].user_id = "another-user";
            } else {
                f.identities[0].provider_id = "999";
            }
            expect((await completeGitHubIdentity(f.auth, identity, f.link, f.sessionToken)).state).toBe("identity_conflict");
            expect(f.identities).toHaveLength(1);
        }
    });

    it("requires authenticator assurance for linking and audits successful changes without credentials", async () => {
        const f = githubFixture();
        f.identities.length = 0;
        f.factors.push({ id: "factor", factor_type: "totp" });
        f.weakenSession();
        expect((await completeGitHubIdentity(f.auth, identity, f.link, f.sessionToken)).state).toBe("verification_required");
        expect(f.identities).toHaveLength(0);
        f.factors.length = 0;
        expect((await completeGitHubIdentity(f.auth, identity, f.link, f.sessionToken)).state).toBe("linked");
        const audits = f.execute.mock.calls.filter(([sql]) => sql.startsWith("INSERT INTO platform_audit_events"));
        expect(audits).toHaveLength(1);
        expect(audits[0][1][1]).toBe("account.github_identity_linked");
        expect(JSON.stringify(audits)).not.toContain(f.sessionToken);
    });

    it.each(["//evil.invalid", "https://evil.invalid", "/\\evil.invalid", "/\nmalformed"])("normalizes unsafe next %s", (next) => {
        expect(githubNextPath(next)).toBe("/account/");
    });
});
