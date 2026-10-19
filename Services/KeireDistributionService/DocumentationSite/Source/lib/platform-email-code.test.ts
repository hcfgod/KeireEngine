import { randomBytes, randomUUID } from "node:crypto";
import { describe, expect, it } from "vitest";
import { createEmailCode, emailCodeLifetimeMs, matchesEmailCode } from "./platform-email-code.mjs";

const key = randomBytes(32);
const context = {
    purpose: "sign_in", userId: randomUUID(), email: "synthetic@example.com",
    passwordDigest: "a".repeat(64), authorizationDigest: "b".repeat(64),
};
const now = 1700000000000;

describe("email verification proof", () => {
    it("accepts the exact context and keeps code/token out of the stored record", () => {
        const challenge = createEmailCode(key, context, now);
        expect(challenge.code).toMatch(/^[0-9]{8}$/);
        expect(challenge.token).toMatch(/^[A-Za-z0-9_-]{43}$/);
        expect(matchesEmailCode(key, context, challenge.record, challenge.token, challenge.code, now)).toBe(true);
        expect(Object.keys(challenge.record).sort()).toEqual(["expiresAt", "issuedAt", "proof", "tokenHash"]);
    });

    it.each([
        { purpose: "enroll" }, { userId: randomUUID() }, { email: "different@example.com" },
        { passwordDigest: "c".repeat(64) }, { authorizationDigest: "d".repeat(64) },
    ])("rejects a changed account, purpose or authorization context", (change) => {
        const challenge = createEmailCode(key, context, now);
        expect(matchesEmailCode(key, { ...context, ...change }, challenge.record,
            challenge.token, challenge.code, now)).toBe(false);
    });

    it("rejects expiry, future issuance and modified validity windows", () => {
        const { record, token, code } = createEmailCode(key, context, now);
        expect(matchesEmailCode(key, context, record, token, code, now + emailCodeLifetimeMs - 1)).toBe(true);
        expect(matchesEmailCode(key, context, record, token, code, now + emailCodeLifetimeMs)).toBe(false);
        expect(matchesEmailCode(key, context, record, token, code, now - 1)).toBe(false);
        const shifted = { ...record, issuedAt: record.issuedAt + 1, expiresAt: record.expiresAt + 1 };
        expect(matchesEmailCode(key, context, shifted, token, code, now + 2)).toBe(false);
    });

    it("rejects other challenge tokens, keys, malformed codes and modified proofs", () => {
        const { record, token, code } = createEmailCode(key, context, now);
        const other = createEmailCode(key, context, now);
        expect(matchesEmailCode(key, context, record, other.token, code, now)).toBe(false);
        expect(matchesEmailCode(randomBytes(32), context, record, token, code, now)).toBe(false);
        expect(matchesEmailCode(key, context, record, token, code + " ", now)).toBe(false);
        expect(matchesEmailCode(key, context, { ...record, proof: "f".repeat(64) }, token, code, now)).toBe(false);
    });

    it("rejects invalid creation contexts and unsafe timestamps", () => {
        expect(() => createEmailCode(Buffer.alloc(1), context, now)).toThrow("protected");
        expect(() => createEmailCode(key, { ...context, purpose: "reset_anything" }, now)).toThrow("context");
        expect(() => createEmailCode(key, context, Number.MAX_SAFE_INTEGER)).toThrow("context");
        expect(() => createEmailCode(key, context, -1)).toThrow("context");
    });
});
