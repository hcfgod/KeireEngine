import { beforeEach, describe, expect, it, vi } from "vitest";
import { handleLocalRecoveryRequest } from "./platform-recovery-http.mjs";
import { recoveryStatus } from "./platform-recovery-store.mjs";
import { enqueueRecoveryDelivery } from "./platform-recovery-delivery.mjs";
import { replaceLocalRecoveredPassword } from "./platform-password-recovery.mjs";

vi.mock("./platform-recovery-store.mjs", () => ({ recoveryStatus: vi.fn() }));
vi.mock("./platform-recovery-delivery.mjs", () => ({ enqueueRecoveryDelivery: vi.fn() }));
vi.mock("./platform-password-recovery.mjs", () => ({ replaceLocalRecoveredPassword: vi.fn() }));
const origin = "https://example.test";
const token = "a".repeat(43);
const service = { authentication: { now: () => 1000 }, recoveryWorker: {} };
const request = (path, method = "POST", body = {}, headers = {}) => new Request(origin + path, {
    method, headers: { origin, "content-type": "application/json", ...headers },
    body: method === "GET" ? undefined : JSON.stringify(body),
});
const handle = (req) => handleLocalRecoveryRequest(service, req, origin, "trusted-client", "correlation");

beforeEach(() => vi.resetAllMocks());

describe("recovery-only browser authorization", () => {
    it("returns generic queued acceptance without account details", async () => {
        vi.mocked(enqueueRecoveryDelivery).mockResolvedValue({ state: "accepted" });
        const response = await handle(request("/account/recovery", "POST", { email: "qa@invalid.example" }));
        expect(response.status).toBe(202);
        expect(await response.json()).toEqual({ message: "If the account exists, a recovery email will be sent." });
    });

    it("exchanges the token only into a secure recovery cookie", async () => {
        vi.mocked(recoveryStatus).mockResolvedValue({ expiresAt: 901000, factors: [] });
        const response = await handle(request("/account/recovery/session", "POST", { token }));
        expect(response.status).toBe(200);
        const cookie = response.headers.get("set-cookie");
        expect(cookie).toContain(`__Host-keire-recovery=${token}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=900`);
        expect(cookie).not.toContain("__Host-keire-session=");
        expect(await response.text()).not.toContain(token);
    });

    it("does not treat a login session as a recovery token", async () => {
        vi.mocked(replaceLocalRecoveredPassword).mockResolvedValue({ state: "invalid_recovery" });
        const response = await handle(request("/account/password", "PATCH", { password: "example password" },
            { cookie: `__Host-keire-session=${token}` }));
        expect(response.status).toBe(400);
        expect(replaceLocalRecoveredPassword).toHaveBeenCalledWith(service.authentication, null,
            "example password", undefined, undefined, "trusted-client");
    });

    it.each([
        [{ origin: "https://attacker.test" }, 403],
        [{ authorization: "Bearer secret" }, 401],
        [{ "content-type": "text/plain" }, 400],
    ])("rejects unsafe mutation metadata", async (headers, status) => {
        expect((await handle(request("/account/recovery", "POST", {}, headers))).status).toBe(status);
        expect(enqueueRecoveryDelivery).not.toHaveBeenCalled();
    });

    it("clears both cookies after replacement and suppresses internal failures", async () => {
        vi.mocked(replaceLocalRecoveredPassword).mockResolvedValue({ state: "updated", signedOut: true });
        const response = await handle(request("/account/password", "PATCH", { password: "example password" },
            { cookie: `__Host-keire-recovery=${token}` }));
        expect(response.status).toBe(200);
        expect(response.headers.getSetCookie()).toHaveLength(2);
        expect(response.headers.getSetCookie().every((value) => value.endsWith("Max-Age=0"))).toBe(true);
        vi.mocked(recoveryStatus).mockRejectedValue(new Error("private database details"));
        const failure = await handle(request("/account/recovery/status", "GET"));
        expect(failure.status).toBe(503);
        expect(await failure.text()).not.toContain("private database details");
    });
});
