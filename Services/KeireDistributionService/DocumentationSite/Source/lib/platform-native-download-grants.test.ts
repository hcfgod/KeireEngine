import { beforeEach, expect, it, vi } from "vitest";
import { withNativeSessionTransaction } from "./platform-native-sessions.mjs";
import { authorizeNativeDownload } from "./platform-native-download-access.mjs";
import { issueNativeDownloadGrant } from "./platform-native-download-grants.mjs";
vi.mock("./platform-native-sessions.mjs", () => ({ withNativeSessionTransaction: vi.fn() }));
vi.mock("./platform-native-download-access.mjs", () => ({ authorizeNativeDownload: vi.fn() }));
const execute = vi.fn();
const verify = vi.fn();
const identity = { clientId: "keire-hub-desktop", sessionId: "session" };
const issue = () => issueNativeDownloadGrant({}, { verify }, "a".repeat(43), identity.clientId, {});
beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(withNativeSessionTransaction).mockImplementation(async (_pool, _token, action) => action({ execute }, identity));
    vi.mocked(authorizeNativeDownload).mockResolvedValue({ version_id: "version", entitlementId: "entitlement",
        deviceSessionId: "device", archive_sha256: "a".repeat(64), archive_size_bytes: "1024", signed_publication: "signed" });
});

it("verifies the bytes and current session before inserting the short-lived grant", async () => {
    execute.mockResolvedValueOnce([[{ live: 1 }]]).mockResolvedValueOnce([])
        .mockResolvedValueOnce([[{ expires_at: "2026-01-01T00:10:00Z" }]]);
    const grant = await issue();
    expect(verify).toHaveBeenCalledWith({ sha256: "a".repeat(64), sizeBytes: 1024 });
    expect(grant.archive_size_bytes).toBe(1024);
    expect(grant.signed_publication).toBe("signed");
    expect(execute.mock.calls[1][0]).toContain("TIMESTAMPADD(MINUTE,10");
    expect(execute.mock.calls[1][1]).toEqual([grant.grant_id, "entitlement", "version", "device"]);
});

it("never grants a missing or damaged file", async () => {
    verify.mockRejectedValue(new Error("private file path"));
    await expect(issue()).rejects.toMatchObject({ code: "marketplace.package_unavailable", status: 503 });
    expect(execute).not.toHaveBeenCalled();
});

it("rejects expiry while hashing, including a string zero result", async () => {
    execute.mockResolvedValue([[{ live: "0" }]]);
    await expect(issue()).rejects.toMatchObject({ code: "account.session_expired" });
    expect(execute).toHaveBeenCalledTimes(1);
});

it("does not inspect file bytes before authorization succeeds", async () => {
    vi.mocked(authorizeNativeDownload).mockRejectedValue(new Error("denied"));
    await expect(issue()).rejects.toThrow("denied");
    expect(verify).not.toHaveBeenCalled();
    expect(execute).not.toHaveBeenCalled();
});

it("rejects invalid credentials without issuing a grant", async () => {
    vi.mocked(withNativeSessionTransaction).mockResolvedValue(null);
    await expect(issue()).rejects.toMatchObject({ status: 401 });
    expect(verify).not.toHaveBeenCalled();
});
