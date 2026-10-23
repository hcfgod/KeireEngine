import { describe, expect, it } from "vitest";
import { createProductionProxyPolicy } from "./platform-production-proxy.mjs";

function request(overrides = {}) {
    const headers = { host: "platform.invalid", "x-forwarded-host": "platform.invalid", "x-forwarded-proto": "https",
        "x-keire-client-ip": "203.0.113.10", forwarded: "for=attacker", "x-forwarded-for": "spoofed", "x-real-ip": "spoofed" };
    return { url: "/account/", method: "GET", headers, rawHeaders: Object.entries(headers).flat(),
        socket: { remoteAddress: "::ffff:127.0.0.1" }, ...overrides };
}
const policy = createProductionProxyPolicy("https://platform.invalid");

describe("production reverse proxy identity", () => {
    it("accepts only trusted local metadata and strips unrelated forwarding chains", () => {
        const input = request();
        expect(policy.authorize(input)).toBe("ip:203.0.113.10");
        expect(input.headers.forwarded).toBeUndefined();
        expect(input.headers["x-forwarded-for"]).toBeUndefined();
        expect(input.headers["x-real-ip"]).toBeUndefined();
        expect(input.headers["x-keire-client-ip"]).toBeUndefined();
    });

    it.each(["host", "x-forwarded-host", "x-forwarded-proto", "x-keire-client-ip"])("rejects duplicated or absent %s", (name) => {
        const duplicate = request();
        duplicate.rawHeaders.push(name, duplicate.headers[name]);
        expect(() => policy.authorize(duplicate)).toThrow();
        const absent = request();
        delete absent.headers[name];
        expect(() => policy.authorize(absent)).toThrow();
    });

    it.each(["203.0.113.1", "127.0.0.2", "::2"])("rejects non-proxy peer %s", (remoteAddress) => {
        expect(() => policy.authorize(request({ socket: { remoteAddress } }))).toThrow();
    });

    it.each(["evil.invalid", "platform.invalid:443"])("requires exact canonical forwarded host %s", (value) => {
        const input = request();
        input.headers["x-forwarded-host"] = value;
        expect(() => policy.authorize(input)).toThrow();
    });

    it.each(["127.0.0.1, 203.0.113.2", "unknown", "fe80::1%lo", "[::1]", "203.0.113.1:123"])("rejects ambiguous client %s", (value) => {
        const input = request();
        input.headers["x-keire-client-ip"] = value;
        expect(() => policy.authorize(input)).toThrow();
    });

    it("normalizes mapped IPv4 and compressed IPv6 rate-limit identities", () => {
        const input = request();
        input.headers["x-keire-client-ip"] = "::ffff:203.0.113.10";
        expect(policy.authorize(input)).toBe("ip:203.0.113.10");
        const ipv6 = request();
        ipv6.headers["x-keire-client-ip"] = "2001:0db8:0000:0000:0000:0000:0000:0001";
        expect(policy.authorize(ipv6)).toBe("ip:2001:db8::1");
    });

    it.each(["https://evil.invalid/", "//evil.invalid/", "/\\evil", "/bad path"])("rejects request-target %s", (url) => {
        expect(() => policy.authorize(request({ url }))).toThrow();
    });

    it("permits only credential-free local readiness probes without forwarded headers", () => {
        expect(policy.healthRequest(request({ url: "/health/", rawHeaders: [], headers: {} }))).toBe(true);
        expect(policy.healthRequest(request({ url: "/health/?code=private" }))).toBe(false);
        expect(policy.healthRequest(request({ url: "/health/", method: "POST" }))).toBe(false);
    });
});
