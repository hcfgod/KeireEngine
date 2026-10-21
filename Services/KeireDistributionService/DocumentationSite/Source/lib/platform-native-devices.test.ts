import { beforeEach, describe, expect, it, vi } from "vitest";
import { withNativeSessionTransaction } from "./platform-native-sessions.mjs";
import { registerNativeDevice } from "./platform-native-devices.mjs";

vi.mock("./platform-native-sessions.mjs", () => ({ withNativeSessionTransaction: vi.fn() }));
const identity = { userId: "user", sessionId: "native-session", clientId: "keire-hub-desktop" };
const execute = vi.fn();
const register = (name = "Desktop") => registerNativeDevice({}, "token", identity.clientId, name);
beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(withNativeSessionTransaction).mockImplementation(async (_pool, _token, action) => action({ execute }, identity));
});

describe("native marketplace device registration", () => {
    it("binds new registration to the authenticated MySQL session", async () => {
        execute.mockResolvedValueOnce([[{ enabled: 1 }]]).mockResolvedValueOnce([[]]).mockResolvedValueOnce([]);
        const result = await register(" Desktop ");
        expect(result.oauthSessionId).toBe(identity.sessionId);
        expect(execute.mock.calls[1][1]).toEqual(["mysql:native-session"]);
        expect(execute.mock.calls[2][1]).toEqual([result.deviceSessionId, "user", "mysql:native-session", "Desktop"]);
    });

    it("reuses the same registration without reviving a revoked device", async () => {
        for (const revoked of [null, "2026-01-01"]) {
            execute.mockReset().mockResolvedValueOnce([[{ enabled: 1 }]])
                .mockResolvedValueOnce([[{ id: "device", user_id: "user", client_type: "hub", revoked_at: revoked }]])
                .mockResolvedValueOnce([]);
            if (revoked) {
                await expect(register()).rejects.toMatchObject({ status: 403 });
                expect(execute).toHaveBeenCalledTimes(2);
            } else {
                expect((await register()).deviceSessionId).toBe("device");
            }
        }
    });

    it("fails closed for disabled feature flags, including string zero", async () => {
        execute.mockResolvedValue([[{ enabled: "0" }]]);
        await expect(register()).rejects.toMatchObject({ status: 503 });
        expect(execute).toHaveBeenCalledTimes(1);
    });

    it("rejects invalid names and missing sessions", async () => {
        await expect(register("\nDesktop")).rejects.toMatchObject({ status: 400 });
        expect(withNativeSessionTransaction).not.toHaveBeenCalled();
        vi.mocked(withNativeSessionTransaction).mockResolvedValue(null);
        await expect(register()).rejects.toMatchObject({ status: 401 });
    });
});
