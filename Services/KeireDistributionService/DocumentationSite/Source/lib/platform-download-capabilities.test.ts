import { describe, expect, it } from "vitest";
import { createDownloadCapabilities } from "./platform-download-capabilities.mjs";
const origin = "https://platform.invalid";
const grant = "11111111-1111-4111-8111-111111111111";
function fixture() {
    let now = 1800000000000;
    const key = Buffer.alloc(32, 7);
    const service = createDownloadCapabilities({ siteOrigin: origin, key, clock: () => now });
    const url = service.issue(grant, new Date(now + 600000).toISOString());
    return { service, url, key, expire: () => { now += 600000; } };
}

describe("private download link capabilities", () => {
    it("binds the grant, origin and expiry without including a user credential", () => {
        const { service, url, key } = fixture();
        key.fill(0);
        expect(service.verify(url)).toEqual({ grantId: grant, expiresMs: 1800000600000 });
        expect(url).not.toContain("access_token");
    });

    it.each([
        (url) => url.replace(grant, "22222222-2222-4222-8222-222222222222"),
        (url) => url.replace("platform.invalid", "other.invalid"),
        (url) => url.replace("1800000600000", "1800000599999"),
        (url) => `${url}&expires=1800000600000`,
        (url) => `${url}&extra=1`,
        (url) => `${url}#fragment`,
        (url) => url.replace("https://", "https://user:password@"),
    ])("rejects altered or ambiguous links", (alter) => {
        const { service, url } = fixture();
        expect(service.verify(alter(url))).toBeNull();
    });

    it("rejects expired links and caps a later database expiry at ten minutes", () => {
        const { service, url, expire } = fixture();
        const capped = service.issue(grant, new Date(1800000600001).toISOString());
        expect(service.verify(capped)?.expiresMs).toBe(1800000600000);
        expire();
        expect(service.verify(url)).toBeNull();
    });

    it("never extends an earlier database expiry and rejects invalid dates", () => {
        const { service } = fixture();
        const earlier = service.issue(grant, new Date(1800000001000).toISOString());
        expect(service.verify(earlier)?.expiresMs).toBe(1800000001000);
        expect(() => service.issue(grant, "invalid")).toThrow();
        expect(() => service.issue(grant, new Date(1800000000000).toISOString())).toThrow();
    });

    it("clears its owned key and rejects use after shutdown", () => {
        const { service, url } = fixture();
        service.close();
        service.close();
        expect(service.verify(url)).toBeNull();
        expect(() => service.issue(grant, new Date(1800000600000).toISOString())).toThrow();
    });
});
