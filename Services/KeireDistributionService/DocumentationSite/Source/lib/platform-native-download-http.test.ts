import { beforeEach, expect, it, vi } from "vitest";
import { authorizeNativeDownloadDelivery } from "./platform-native-download-delivery.mjs";
import { createNativeDownloadHttp } from "./platform-native-download-http.mjs";

vi.mock("./platform-native-download-delivery.mjs", () => ({ authorizeNativeDownloadDelivery: vi.fn() }));
const verify = vi.fn();
const closed = vi.fn();
const read = vi.fn();
const address = "https://example.test/marketplace/v1/downloads/grant";
const handler = createNativeDownloadHttp({ pool: {}, clientId: "hub", capabilities: { verify }, packageStore: { read } });

beforeEach(() => {
    vi.resetAllMocks();
    verify.mockReturnValue({ grantId: "grant" });
    vi.mocked(authorizeNativeDownloadDelivery).mockResolvedValue({ archive_sha256: "a".repeat(64), archive_size_bytes: "3" });
    read.mockImplementation(async function* () {
        try {
            yield new Uint8Array([1, 2, 3]);
        } finally {
            closed();
        }
    });
});

it("streams bytes after authorization and rechecks after file verification", async () => {
    const response = await handler(new Request(address));
    expect(response.status).toBe(200);
    expect([...new Uint8Array(await response.arrayBuffer())]).toEqual([1, 2, 3]);
    expect(authorizeNativeDownloadDelivery).toHaveBeenCalledTimes(2);
    expect(response.headers.get("cache-control")).toBe("private, no-store");
    expect(closed).toHaveBeenCalledOnce();
});

it("rejects invalid links without reading storage or the database", async () => {
    verify.mockReturnValue(null);
    expect((await handler(new Request(address))).status).toBe(403);
    expect(read).not.toHaveBeenCalled();
    expect(authorizeNativeDownloadDelivery).not.toHaveBeenCalled();
});

it("closes the verified file if the link expires during hashing", async () => {
    verify.mockReturnValueOnce({ grantId: "grant" }).mockReturnValue(null);
    expect((await handler(new Request(address))).status).toBe(403);
    expect(closed).toHaveBeenCalledOnce();
});

it("does not expose private storage errors", async () => {
    read.mockImplementation(async function* () { throw new Error("private disk path"); });
    const response = await handler(new Request(address));
    expect(response.status).toBe(503);
    expect(await response.text()).not.toContain("private disk path");
});

it("closes the file when the consumer cancels", async () => {
    const response = await handler(new Request(address));
    await response.body.cancel();
    expect(closed).toHaveBeenCalledOnce();
});

it("rejects unsupported methods before authorization", async () => {
    expect((await handler(new Request(address, { method: "HEAD" }))).status).toBe(405);
    expect(verify).not.toHaveBeenCalled();
});

it("closes an idle stream when the request disconnects without another pull", async () => {
    const abort = new AbortController();
    const response = await handler(new Request(address, { signal: abort.signal }));
    abort.abort();
    await expect(response.arrayBuffer()).rejects.toThrow("Download cancelled");
    await vi.waitFor(() => expect(closed).toHaveBeenCalledOnce());
});

it("closes storage if access is revoked during file verification", async () => {
    vi.mocked(authorizeNativeDownloadDelivery)
        .mockResolvedValueOnce({ archive_sha256: "a".repeat(64), archive_size_bytes: "3" })
        .mockRejectedValueOnce(new Error("denied"));
    const response = await handler(new Request(address));
    expect(response.status).toBe(503);
    expect(closed).toHaveBeenCalledOnce();
});

it("shutdown cancels idle streams, waits for cleanup and rejects new downloads", async () => {
    const service = createNativeDownloadHttp({ pool: {}, clientId: "hub", capabilities: { verify }, packageStore: { read } });
    const response = await service(new Request(address));
    const shutdown = service.close();
    expect(service.close()).toBe(shutdown);
    await shutdown;
    expect(closed).toHaveBeenCalledOnce();
    await expect(response.arrayBuffer()).rejects.toThrow("Download cancelled");
    expect((await service(new Request(address))).status).toBe(503);
});

it("shutdown waits for file verification already in progress and closes its handle", async () => {
    let resume;
    read.mockImplementation(async function* () {
        try {
            await new Promise((resolve) => { resume = resolve; });
            yield new Uint8Array([1, 2, 3]);
        } finally {
            closed();
        }
    });
    const service = createNativeDownloadHttp({ pool: {}, clientId: "hub", capabilities: { verify }, packageStore: { read } });
    const pending = service(new Request(address));
    await vi.waitFor(() => expect(resume).toBeTypeOf("function"));
    let finished = false;
    const shutdown = service.close().then(() => { finished = true; });
    await Promise.resolve();
    expect(finished).toBe(false);
    resume();
    expect((await pending).status).toBe(503);
    await shutdown;
    expect(closed).toHaveBeenCalledOnce();
});
