import { describe, expect, it } from "vitest";
import { handleGitHubRequest, createLocalGitHubService } from "./platform-github-http.mjs";
import { githubFixture } from "./platform-github-test-fixture";

const origin = "https://platform.invalid";
function request(path = "/account/github/", method = "GET", headers = {}) {
    return new Request(`${origin}${path}`, { method, headers });
}
async function begin(f, cookie = "") {
    const result = await handleGitHubRequest(f.auth, f.provider,
        request("/account/github/?next=%2Faccount%2F", "POST", { origin, cookie }), "loopback");
    return { state: new URL(result.headers.get("location")).searchParams.get("state"),
        cookie: result.headers.get("set-cookie").split(";")[0] };
}

describe("GitHub browser boundary", () => {
    it("accepts GitHub's issuer and rejects foreign, empty, and duplicate issuers before exchange", async () => {
        for (const issuer of ["https://evil.invalid", "", "https://github.com/login/oauth&iss=https://github.com/login/oauth"]) {
            const f = githubFixture();
            const flow = await begin(f);
            const response = await handleGitHubRequest(f.auth, f.provider,
                request(`/account/github/callback/?state=${flow.state}&code=code123&iss=${issuer}`,
                    "GET", { cookie: flow.cookie }), "loopback");
            expect(response.status).toBe(400);
            expect(f.provider.identity).not.toHaveBeenCalled();
        }
        const f = githubFixture();
        const flow = await begin(f);
        const response = await handleGitHubRequest(f.auth, f.provider,
            request(`/account/github/callback/?state=${flow.state}&code=code123&iss=https%3A%2F%2Fgithub.com%2Flogin%2Foauth`,
                "GET", { cookie: flow.cookie }), "loopback");
        expect(response.status).toBe(303);
        expect(response.headers.get("location")).toBe("/account/");
        expect(f.provider.identity).toHaveBeenCalledTimes(1);
    });

    it("GET only renders explicit confirmation; POST requires exact origin", async () => {
        const f = githubFixture();
        const prompt = await handleGitHubRequest(f.auth, f.provider,
            request("/account/github/", "GET", { cookie: `__Host-keire-session=${f.sessionToken}` }), "loopback");
        expect(prompt.status).toBe(200);
        expect(prompt.headers.get("content-security-policy")).toContain("form-action 'self' https://github.com;");
        expect(await prompt.text()).toContain("Link my GitHub identity");
        expect(f.records.size).toBe(0);
        for (const requestOrigin of ["https://evil.invalid", "null", ""]) {
            expect((await handleGitHubRequest(f.auth, f.provider,
                request("/account/github/", "POST", { origin: requestOrigin }), "loopback")).status).toBe(403);
        }
        expect(f.records.size).toBe(0);
    });

    it("sets browser-only state cookie and never exposes the verifier", async () => {
        const f = githubFixture();
        const response = await handleGitHubRequest(f.auth, f.provider,
            request("/account/github/", "POST", { origin }), "loopback");
        expect(response.status).toBe(303);
        expect(response.headers.get("set-cookie")).toMatch(/^__Host-keire-github=.{43}; Path=\/; HttpOnly; Secure; SameSite=Lax; Max-Age=600$/);
        expect(response.headers.get("referrer-policy")).toBe("no-referrer");
        expect(response.headers.get("cache-control")).toBe("private, no-store");
    });

    it("accepts a valid callback once, creates a local cookie, and denies replay before GitHub", async () => {
        const f = githubFixture();
        const flow = await begin(f);
        const callback = () => request(`/account/github/callback/?state=${flow.state}&code=code123`, "GET", { cookie: flow.cookie });
        const response = await handleGitHubRequest(f.auth, f.provider, callback(), "loopback");
        expect(response.status).toBe(303);
        expect(response.headers.get("location")).toBe("/account/");
        expect(response.headers.get("set-cookie")).toContain(`__Host-keire-session=${"n".repeat(43)}`);
        expect((await handleGitHubRequest(f.auth, f.provider, callback(), "loopback")).status).toBe(400);
        expect(f.provider.identity).toHaveBeenCalledTimes(1);
    });

    it("requires the account factor after GitHub rather than bypassing MFA", async () => {
        const f = githubFixture();
        f.factors.push({ id: "totp", factor_type: "totp" });
        const flow = await begin(f);
        const response = await handleGitHubRequest(f.auth, f.provider,
            request(`/account/github/callback/?state=${flow.state}&code=code123`, "GET", { cookie: flow.cookie }), "loopback");
        expect(response.headers.get("location")).toBe("/account/mfa/challenge/?next=%2Faccount%2F");
        expect(response.headers.get("set-cookie")).toContain("__Host-keire-challenge=");
        expect(f.auth.issueSession).not.toHaveBeenCalled();
    });

    it("denies linking if the original session expires during GitHub authorization", async () => {
        const f = githubFixture();
        const sessionCookie = `__Host-keire-session=${f.sessionToken}`;
        const flow = await begin(f, sessionCookie);
        f.expireSession();
        f.identities.length = 0;
        const response = await handleGitHubRequest(f.auth, f.provider,
            request(`/account/github/callback/?state=${flow.state}&code=code123`, "GET", { cookie: `${flow.cookie}; ${sessionCookie}` }), "loopback");
        expect(response.status).toBe(401);
        expect(f.identities).toHaveLength(0);
    });

    it("rejects expiry during provider exchange without creating a session", async () => {
        const f = githubFixture();
        const flow = await begin(f);
        f.provider.identity.mockImplementation(async () => {
            f.advance(600000);
            return { subject: "12345", login: "fixture", email: f.account.email };
        });
        const response = await handleGitHubRequest(f.auth, f.provider,
            request(`/account/github/callback/?state=${flow.state}&code=code123`, "GET", { cookie: flow.cookie }), "loopback");
        expect(response.status).toBe(401);
        expect(f.auth.issueSession).not.toHaveBeenCalled();
    });

    it("rejects duplicate callback parameters and duplicate cookies before contacting GitHub", async () => {
        const f = githubFixture();
        const flow = await begin(f);
        for (const [query, cookie] of [[`state=${flow.state}&state=${flow.state}&code=code123`, flow.cookie],
            [`state=${flow.state}&code=code123`, `${flow.cookie}; ${flow.cookie}`]]) {
            expect((await handleGitHubRequest(f.auth, f.provider,
                request(`/account/github/callback/?${query}`, "GET", { cookie }), "loopback")).status).toBe(400);
        }
        expect(f.provider.identity).not.toHaveBeenCalled();
    });

    it("handles cancellation without echoing the provider error", async () => {
        const f = githubFixture();
        const flow = await begin(f);
        const response = await handleGitHubRequest(f.auth, f.provider,
            request(`/account/github/callback/?state=${flow.state}&error=access_denied&error_description=private-secret`, "GET", { cookie: flow.cookie }), "loopback");
        expect(response.status).toBe(400);
        expect(await response.text()).not.toContain("private-secret");
        expect(f.provider.identity).not.toHaveBeenCalled();
    });

    it("gives unlinked users an explicit create-or-link workflow without email auto-link", async () => {
        const f = githubFixture();
        f.identities.length = 0;
        const flow = await begin(f);
        const response = await handleGitHubRequest(f.auth, f.provider,
            request(`/account/github/callback/?state=${flow.state}&code=code123`, "GET", { cookie: flow.cookie }), "loopback");
        expect(response.status).toBe(409);
        expect(await response.text()).toContain("Create an account");
        expect(f.auth.issueSession).not.toHaveBeenCalled();
    });

    it("closes idempotently and refuses new authorization afterward", async () => {
        const f = githubFixture();
        const service = createLocalGitHubService({ authentication: f.auth, settings: {
            siteOrigin: origin, clientId: "Iv1.clientfixture", clientSecret: Buffer.alloc(40, 97),
        } });
        const first = service.close();
        expect(service.close()).toBe(first);
        await first;
        expect((await service.handle(request(), "loopback")).status).toBe(503);
        expect(await service.handle(request("/unrelated"), "loopback")).toBeNull();
    });
});
