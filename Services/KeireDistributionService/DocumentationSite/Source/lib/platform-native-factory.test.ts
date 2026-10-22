import { beforeEach, expect, it, vi } from "vitest";
import { loadWindowsNativeSecrets } from "./platform-native-secrets.mjs";
import { loadWindowsDownloadSecrets } from "./platform-download-secrets.mjs";
import { createLocalNativeService } from "./platform-native-service.mjs";
import { windowsNativeServiceFactory } from "./platform-native-factory.mjs";

vi.mock("./platform-native-secrets.mjs", () => ({ loadWindowsNativeSecrets: vi.fn() }));
vi.mock("./platform-download-secrets.mjs", () => ({ loadWindowsDownloadSecrets: vi.fn() }));
vi.mock("./platform-native-service.mjs", () => ({ createLocalNativeService: vi.fn() }));

beforeEach(() => {
    vi.resetAllMocks();
    vi.mocked(loadWindowsNativeSecrets).mockResolvedValue({ siteOrigin: "https://platform.invalid", clientId: "hub" });
});

it("captures download settings, binds them to the identity origin and clears the temporary key", async () => {
    const key = Buffer.alloc(32, 7);
    const store = {};
    const downloads = { decryptScriptPath: "reader", settingsDirectory: "protected", packageStore: store };
    const factory = windowsNativeServiceFactory({ powerShellExecutable: "powershell", downloads });
    downloads.settingsDirectory = "changed";
    vi.mocked(loadWindowsDownloadSecrets).mockResolvedValue(key);
    let copied;
    vi.mocked(createLocalNativeService).mockImplementation((options) => {
        copied = Buffer.from(options.downloadKey);
        expect(options.packageStore).toBe(store);
        return { handle() {} };
    });
    await factory({ pool: {}, authentication: {} });
    expect(loadWindowsDownloadSecrets).toHaveBeenCalledWith({ powerShellExecutable: "powershell",
        decryptScriptPath: "reader", settingsDirectory: "protected", siteOrigin: "https://platform.invalid" });
    expect(copied).toEqual(Buffer.alloc(32, 7));
    expect(key).toEqual(Buffer.alloc(32));
});

it("clears a loaded key when service construction fails", async () => {
    const key = Buffer.alloc(32, 7);
    vi.mocked(loadWindowsDownloadSecrets).mockResolvedValue(key);
    vi.mocked(createLocalNativeService).mockImplementation(() => { throw new Error("startup failed"); });
    await expect(windowsNativeServiceFactory({ downloads: {} })({ pool: {}, authentication: {} })).rejects.toThrow("startup failed");
    expect(key).toEqual(Buffer.alloc(32));
});

it("keeps download configuration optional without loading a key", async () => {
    await windowsNativeServiceFactory({})({ pool: {}, authentication: {} });
    expect(loadWindowsDownloadSecrets).not.toHaveBeenCalled();
});
