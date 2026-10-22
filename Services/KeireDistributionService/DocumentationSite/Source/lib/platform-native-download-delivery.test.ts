import { beforeEach, expect, it, vi } from "vitest";
import { nativeAccountBinding } from "./platform-native-authorization.mjs";
import { authorizeNativeDownload } from "./platform-native-download-access.mjs";
import { authorizeNativeDownloadDelivery } from "./platform-native-download-delivery.mjs";

vi.mock("./platform-native-download-access.mjs", () => ({ authorizeNativeDownload: vi.fn() }));

const execute = vi.fn();
const connection = {
    execute,
    beginTransaction: vi.fn(),
    commit: vi.fn(),
    rollback: vi.fn(),
    release: vi.fn(),
    destroy: vi.fn(),
};
const pool = { getConnection: async () => connection };
const account = { id: "owner", email: "owner@invalid.example", password_hash: "hash", email_confirmed_at: "2026-01-01" };
const grant = {
    live: 1, revoked_ms: null, client_id: "hub", expires_ms: 2000,
    user_id: account.id, native_session_id: "native", version_id: "version",
    device_id: "device", organization_id: null, entitlement_id: "entitlement",
    account_binding: nativeAccountBinding(account, []),
};
const deliver = (clock = () => 1000) => authorizeNativeDownloadDelivery(pool, "grant", "hub", clock);

function results(first = grant, finalLive = 1, currentAccount = account) {
    execute.mockResolvedValueOnce([[first]])
        .mockResolvedValueOnce([[currentAccount]])
        .mockResolvedValueOnce([[]])
        .mockResolvedValueOnce([[{ live: finalLive }]]);
}

beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(authorizeNativeDownload).mockResolvedValue({ entitlementId: "entitlement", archive_sha256: "hash" });
});

it("rechecks current access using only the grant's recorded identity", async () => {
    results();
    await expect(deliver()).resolves.toMatchObject({ archive_sha256: "hash" });
    expect(authorizeNativeDownload).toHaveBeenCalledWith(connection, { userId: "owner", sessionId: "native" }, {
        versionId: "version", deviceSessionId: "device", organizationId: null,
    });
    expect(connection.commit).toHaveBeenCalledOnce();
    expect(connection.release).toHaveBeenCalledOnce();
});

it.each([
    undefined,
    { ...grant, live: "0" },
    { ...grant, revoked_ms: 900 },
    { ...grant, client_id: "other" },
    { ...grant, expires_ms: 1000 },
])("rejects unavailable grants and sessions before checking asset access: %j", async (record) => {
    execute.mockResolvedValueOnce([[record]]);
    await expect(deliver()).rejects.toMatchObject({ status: 403 });
    expect(authorizeNativeDownload).not.toHaveBeenCalled();
    expect(connection.rollback).toHaveBeenCalledOnce();
    expect(connection.commit).not.toHaveBeenCalled();
});

it.each([
    { ...account, deleted_at: "2026-01-01" },
    { ...account, password_hash: "changed" },
])("rejects disabled accounts and changed security bindings", async (changed) => {
    results(grant, 1, changed);
    await expect(deliver()).rejects.toMatchObject({ status: 403 });
    expect(authorizeNativeDownload).not.toHaveBeenCalled();
});

it("does not substitute another entitlement for the one that issued the link", async () => {
    results();
    vi.mocked(authorizeNativeDownload).mockResolvedValue({ entitlementId: "replacement" });
    await expect(deliver()).rejects.toMatchObject({ status: 403 });
    expect(connection.rollback).toHaveBeenCalledOnce();
});

it("propagates revoked asset access without committing", async () => {
    results();
    vi.mocked(authorizeNativeDownload).mockRejectedValue(new Error("revoked"));
    await expect(deliver()).rejects.toThrow("revoked");
    expect(connection.rollback).toHaveBeenCalledOnce();
});

it("rejects grant expiry during authorization", async () => {
    results(grant, 0);
    await expect(deliver()).rejects.toMatchObject({ status: 403 });
});

it("rejects native session expiry during authorization", async () => {
    results();
    const clock = vi.fn().mockReturnValueOnce(1000).mockReturnValueOnce(1000).mockReturnValue(2000);
    await expect(deliver(clock)).rejects.toMatchObject({ status: 403 });
});
