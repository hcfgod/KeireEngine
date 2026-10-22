import { beforeEach, expect, it, vi } from "vitest";
import { registerNativeDevice } from "./platform-native-devices.mjs";
import { createNativeMarketplaceHttp } from "./platform-native-marketplace-http.mjs";
import { issueNativeDownloadGrant } from "./platform-native-download-grants.mjs";
vi.mock("./platform-native-download-grants.mjs", () => ({ issueNativeDownloadGrant: vi.fn() }));
vi.mock("./platform-native-devices.mjs", () => ({ registerNativeDevice: vi.fn() }));
const origin = "https://platform.invalid";
const handle = createNativeMarketplaceHttp({ pool: {}, siteOrigin: origin, clientId: "keire-hub-desktop" });
const request = (input = { operation: "session.register", deviceName: "Desktop" }, headers = {}) => new Request(
    origin + "/functions/v1/marketplace-hub", { method: "POST", headers: {
        authorization: `Bearer ${"a".repeat(43)}`, "content-type": "application/json", ...headers,
    }, body: JSON.stringify(input) });
beforeEach(() => vi.resetAllMocks());

it("returns the existing marketplace device response contract", async () => {
    vi.mocked(registerNativeDevice).mockResolvedValue({ deviceSessionId: "device", oauthSessionId: "session" });
    const response = await handle(request());
    expect(response.status).toBe(201);
    expect(await response.json()).toEqual({ data: { deviceSessionId: "device", oauthSessionId: "session" } });
    expect(response.headers.get("cache-control")).toContain("no-store");
});

it.each([{ cookie: "browser" }, { origin }, { apikey: "legacy" }, { authorization: "invalid" }])(
    "rejects inappropriate credentials before registration", async (headers) => {
        expect((await handle(request(undefined, headers))).status).toBeGreaterThanOrEqual(400);
        expect(registerNativeDevice).not.toHaveBeenCalled();
    });

it("rejects extra actor fields and redacts internal failures", async () => {
    expect((await handle(request({ operation: "session.register", deviceName: "Desktop", userId: "other" } as any))).status).toBe(400);
    expect(registerNativeDevice).not.toHaveBeenCalled();
    vi.mocked(registerNativeDevice).mockRejectedValue(new Error("private database detail"));
    const response = await handle(request());
    expect(response.status).toBe(503);
    expect(await response.text()).not.toContain("private database");
});

it("returns the existing download grant contract with a signed delivery URL", async () => {
    const issue = vi.fn().mockReturnValue("https://platform.invalid/marketplace/v1/downloads/signed");
    const store = {};
    const service = createNativeMarketplaceHttp({ pool: {}, siteOrigin: origin, clientId: "hub",
        capabilities: { issue }, packageStore: store });
    const grant = { grant_id: "grant", expires_at: "expiry", archive_sha256: "hash", signed_publication: "publication" };
    vi.mocked(issueNativeDownloadGrant).mockResolvedValue(grant);
    const response = await service(request({ operation: "download.issue", versionId: "version", deviceSessionId: "device" } as any));
    expect(response.status).toBe(201);
    expect((await response.json()).data).toEqual({ ...grant, url: issue.mock.results[0].value });
    expect(issue).toHaveBeenCalledWith("grant", "expiry");
    expect(issueNativeDownloadGrant).toHaveBeenCalledWith({}, store, "a".repeat(43), "hub", {
        versionId: "version", deviceSessionId: "device", organizationId: null,
    });
});

it("fails closed when download storage is not configured", async () => {
    const response = await handle(request({ operation: "download.issue", versionId: "version", deviceSessionId: "device" } as any));
    expect(response.status).toBe(503);
    expect(issueNativeDownloadGrant).not.toHaveBeenCalled();
});
