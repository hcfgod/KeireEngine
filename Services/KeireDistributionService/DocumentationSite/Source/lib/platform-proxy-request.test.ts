import { describe, expect, it } from "vitest";
import { PassThrough } from "node:stream";
import { Socket } from "node:net";
import { createRequestFromNodeRequest } from "astro/app/node";
import { canonicalPlatformRequest } from "./platform-proxy-request.mjs";

describe("verified proxy request origin", () => {
    it("corrects the actual Astro adapter's loopback HTTP URL", () => {
        const incoming = new PassThrough();
        Object.assign(incoming, { socket: new Socket(), method: "GET", url: "/account/github/?next=%2Faccount%2F",
            headers: { host: "platform.invalid", "x-forwarded-proto": "https" } });
        const request = createRequestFromNodeRequest(incoming as any);
        expect(new URL(request.url).protocol).toBe("http:");
        const canonical = canonicalPlatformRequest(request, "https://platform.invalid");
        expect(canonical.url).toBe("https://platform.invalid/account/github/?next=%2Faccount%2F");
        incoming.destroy();
    });

    it("preserves mutation bodies, cookies and Origin for CSRF checks", async () => {
        const request = new Request("http://platform.invalid/account/action/", { method: "POST", body: "a=1",
            headers: { origin: "https://attacker.invalid", cookie: "session=fixture" } });
        const canonical = canonicalPlatformRequest(request, "https://platform.invalid");
        expect(canonical.method).toBe("POST");
        expect(await canonical.text()).toBe("a=1");
        expect(canonical.headers.get("origin")).toBe("https://attacker.invalid");
        expect(canonical.headers.get("cookie")).toBe("session=fixture");
    });

    it("does not infer trust from forged forwarded headers", () => {
        const request = new Request("http://platform.invalid/", { headers: { "x-forwarded-proto": "https" } });
        expect(canonicalPlatformRequest(request, undefined)).toBe(request);
        expect(() => canonicalPlatformRequest(request, "https://other.invalid")).toThrow();
        expect(() => canonicalPlatformRequest(request, "http://platform.invalid")).toThrow();
    });
});
