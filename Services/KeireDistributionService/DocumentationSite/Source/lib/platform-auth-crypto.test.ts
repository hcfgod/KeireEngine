import bcrypt from "bcryptjs";
import { describe, expect, it } from "vitest";
import { matchingTotpCounter, newOpaqueToken, tokenDigest, verifyImportedPassword } from "./platform-auth-crypto.mjs";

const rfcSecret = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";

describe("RFC 6238 compatibility", () => {
    it.each([
        [59, "94287082"], [1111111109, "07081804"], [1111111111, "14050471"],
        [1234567890, "89005924"], [2000000000, "69279037"], [20000000000, "65353130"],
    ])("accepts the published SHA-1 vector at %i", (seconds, code) => {
        expect(matchingTotpCounter(rfcSecret, code, Number(seconds) * 1000, null, 8))
            .toBe(Math.floor(Number(seconds) / 30));
    });

    it("rejects replays and codes outside the bounded time window", () => {
        expect(matchingTotpCounter(rfcSecret, "287082", 59000)).toBe(1);
        expect(matchingTotpCounter(rfcSecret, "287082", 59000, 1)).toBeNull();
        expect(matchingTotpCounter(rfcSecret, "287082", 120000)).toBeNull();
        expect(matchingTotpCounter(rfcSecret, "287082", 60000)).toBe(1);
    });

    it("rejects malformed codes and times", () => {
        expect(matchingTotpCounter(rfcSecret, "28708x", 59000)).toBeNull();
        expect(matchingTotpCounter(rfcSecret, "287082", -1)).toBeNull();
        expect(matchingTotpCounter(rfcSecret, "287082", NaN)).toBeNull();
    });
});

describe("imported password compatibility", () => {
    it("verifies bcrypt $2a$ and $2b$ without modifying existing hashes", async () => {
        const hash = await bcrypt.hash("synthetic password", 4);
        await expect(verifyImportedPassword("synthetic password", hash)).resolves.toBe(true);
        await expect(verifyImportedPassword("synthetic password", hash.replace("$2b$", "$2a$"))).resolves.toBe(true);
        await expect(verifyImportedPassword("wrong password", hash)).resolves.toBe(false);
    });

    it("rejects bcrypt truncation collisions, including multibyte strings", async () => {
        const password = "a".repeat(72);
        const hash = await bcrypt.hash(password, 4);
        await expect(verifyImportedPassword(password + "different", hash)).resolves.toBe(false);
        await expect(verifyImportedPassword("é".repeat(37), hash)).resolves.toBe(false);
    });
});

it("creates independent opaque tokens and stores only their digests", () => {
    const first = newOpaqueToken();
    const second = newOpaqueToken();
    expect(first).not.toBe(second);
    expect(tokenDigest(first)).toMatch(/^[a-f0-9]{64}$/);
    expect(tokenDigest(first)).not.toBe(first);
    expect(tokenDigest("malformed")).toBeNull();
});
