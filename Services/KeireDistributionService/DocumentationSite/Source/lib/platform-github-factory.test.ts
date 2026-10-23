import { describe, expect, it, vi } from "vitest";

const mocks = vi.hoisted(() => ({ load: vi.fn(), create: vi.fn() }));
vi.mock("./platform-github-secrets.mjs", () => ({ loadWindowsGitHubSecrets: mocks.load }));
vi.mock("./platform-github-http.mjs", () => ({ createLocalGitHubService: mocks.create }));
import { windowsGitHubServiceFactory } from "./platform-github-factory.mjs";

describe("GitHub protected service factory", () => {
    it("captures configuration and clears the bootstrap secret after the service copies it", async () => {
        const options = { siteOrigin: "https://platform.invalid", settingsDirectory: "original" };
        const secret = Buffer.alloc(40, 1);
        const authentication = {};
        const service = { close: () => {} };
        mocks.load.mockResolvedValue({ clientSecret: secret });
        mocks.create.mockImplementation(({ settings }) => {
            expect(settings.clientSecret[0]).toBe(1);
            return service;
        });
        const factory = windowsGitHubServiceFactory(options);
        options.settingsDirectory = "changed";
        expect(await factory({ authentication })).toBe(service);
        expect(mocks.load).toHaveBeenLastCalledWith({ siteOrigin: "https://platform.invalid", settingsDirectory: "original" });
        expect(secret.every((value) => value === 0)).toBe(true);
    });

    it("clears loaded secrets even when service initialization fails", async () => {
        const secret = Buffer.alloc(40, 1);
        mocks.load.mockResolvedValue({ clientSecret: secret });
        mocks.create.mockImplementation(() => { throw new Error("fixture failure"); });
        await expect(windowsGitHubServiceFactory({})({ authentication: {} })).rejects.toThrow("fixture failure");
        expect(secret.every((value) => value === 0)).toBe(true);
    });
});
