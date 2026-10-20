import { randomBytes } from "node:crypto";
import { describe, expect, it, vi } from "vitest";
import { enqueueRegistration, registrationDetails, registrationLifetimeMs } from "./platform-registration.mjs";
import { openAccountRecord } from "./platform-account-import.mjs";
import { tokenDigest, verifyImportedPassword } from "./platform-auth-crypto.mjs";

const input = {
    email: " Person@Example.com ",
    displayName: " Example Person ",
    password: "synthetic-password",
    accepted: true,
};

describe("local registration staging", () => {
    it("normalizes account fields without trimming the password", () => {
        expect(registrationDetails({ ...input, password: " password with spaces " })).toEqual({
            email: "person@example.com", displayName: "Example Person", password: " password with spaces ",
        });
    });

    it("rejects missing consent, injection characters, empty names and bcrypt truncation", () => {
        for (const change of [
            { accepted: false }, { email: "person@example.com\r\nBcc:other@example.com" },
            { displayName: " " }, { displayName: "name\u0000" }, { password: "short" },
            { password: "é".repeat(37) }, { password: "a".repeat(73) },
        ]) {
            expect(registrationDetails({ ...input, ...change })).toBeNull();
        }
    });

    it("returns the same accepted result when rate limited without storing or hashing a request", async () => {
        const authentication = {
            now: () => 1000,
            reserveAttempt: vi.fn().mockResolvedValue(false),
            pool: { execute: vi.fn() },
        };
        expect(await enqueueRegistration(authentication, input, "qa")).toEqual({ state: "accepted" });
        expect(authentication.pool.execute).not.toHaveBeenCalled();
    });

    it("seals the pending password hash and confirmation token without creating an account", async () => {
        const encryptionKey = randomBytes(32);
        let stored: any[];
        const execute = vi.fn(async (_sql: string, values: any[]) => {
            stored = values.map((value) => Buffer.isBuffer(value) ? Buffer.from(value) : value);
        });
        const authentication = {
            encryptionKey, now: () => 1000, reserveAttempt: vi.fn().mockResolvedValue(true), pool: { execute },
        };
        expect(await enqueueRegistration(authentication, input, "qa")).toEqual({ state: "accepted" });
        expect(execute).toHaveBeenCalledTimes(1);
        expect(execute.mock.calls[0][0]).toContain("INSERT INTO platform_registration_requests");
        const [id, digest, sealed, created, expires] = stored!;
        const pending = openAccountRecord(sealed, encryptionKey, `registration:${id}`);
        expect(pending.email).toBe("person@example.com");
        expect(pending.displayName).toBe("Example Person");
        expect(Object.hasOwn(pending, "password")).toBe(false);
        expect(await verifyImportedPassword(input.password, pending.passwordHash)).toBe(true);
        expect(tokenDigest(pending.token)).toBe(digest);
        expect(expires - created).toBe(registrationLifetimeMs);
        expect(pending.termsAcceptedAt).toBe(created);
        expect(execute.mock.calls[0][1][2].every((byte: number) => byte === 0)).toBe(true);
    });
});
