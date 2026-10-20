import { randomBytes } from "node:crypto";
import { describe, expect, it } from "vitest";
import { createValidatorCapabilities } from "./platform-validator-capabilities.mjs";

const origin = "https://platform.invalid";
const claims = { uploadId: "11111111-1111-4111-8111-111111111111",
    workerId: "validator-one", attempt: 1, purpose: "package" };

describe("validator file capabilities", () => {
    it("binds an opaque signed link to one upload, worker, attempt, and operation", () => {
        const links = createValidatorCapabilities(randomBytes(32), origin);
        const address = links.issue(claims);
        expect(links.verify(address, "GET")).toEqual({ version: 1, ...claims });
        expect(() => links.verify(address, "PUT")).toThrow("invalid or expired");
        const evidence = links.issue({ ...claims, purpose: "evidence" });
        expect(links.verify(evidence, "PUT").purpose).toBe("evidence");
        expect(() => links.verify(evidence, "GET")).toThrow();
        links.close();
    });

    it("rejects another key and altered payload or signature", () => {
        const links = createValidatorCapabilities(randomBytes(32), origin);
        const other = createValidatorCapabilities(randomBytes(32), origin);
        const address = links.issue(claims);
        expect(() => other.verify(address, "GET")).toThrow();
        const url = new URL(address);
        const [payload, signature] = url.searchParams.get("token")!.split(".");
        const altered = Buffer.from(JSON.stringify({ version: 1, ...claims, attempt: 2 })).toString("base64url");
        url.searchParams.set("token", `${altered}.${signature}`);
        expect(() => links.verify(url.href, "GET")).toThrow();
        url.searchParams.set("token", `${payload}.${"a".repeat(43)}`);
        expect(() => links.verify(url.href, "GET")).toThrow();
        links.close();
        other.close();
    });

    it.each([
        (url) => url.replace(origin, "https://other.invalid"),
        (url) => url.replace("https:", "http:"),
        (url) => url.replace("/package/", "/evidence/"),
        (url) => `${url}&token=second`,
        (url) => `${url}&other=value`,
        (url) => `${url}#fragment`,
        (url) => url.replace("https://", "https://name:password@"),
        () => "invalid-url",
    ])("rejects a changed destination or ambiguous credentials", (change) => {
        const links = createValidatorCapabilities(randomBytes(32), origin);
        expect(() => links.verify(change(links.issue(claims)), "GET")).toThrow("invalid or expired");
        links.close();
    });

    it("owns its key copy and rejects all operations after close", () => {
        const key = randomBytes(32);
        const links = createValidatorCapabilities(key, origin);
        const address = links.issue(claims);
        key.fill(0);
        expect(links.verify(address, "GET").attempt).toBe(1);
        links.close();
        links.close();
        expect(() => links.verify(address, "GET")).toThrow();
        expect(() => links.issue(claims)).toThrow();
    });

    it.each([0, 6, 1.5])("rejects invalid lease attempt %s", (attempt) => {
        const links = createValidatorCapabilities(randomBytes(32), origin);
        expect(() => links.issue({ ...claims, attempt })).toThrow();
        links.close();
    });
});
