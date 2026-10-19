import { randomBytes } from "node:crypto";
import { describe, expect, it } from "vitest";
import { accountImportRecords, mysqlUtcTimestamp, openAccountRecord, sealAccountRecord } from "./platform-account-import.mjs";

describe("account timestamp migration", () => {
    it("keeps microseconds while converting offsets to UTC", () => {
        expect(mysqlUtcTimestamp("2026-10-19T01:02:03.123456-06:00"))
            .toBe("2026-10-19 07:02:03.123456");
        expect(mysqlUtcTimestamp("2026-01-01T00:00:00.1+05:45"))
            .toBe("2025-12-31 18:15:00.100000");
        expect(mysqlUtcTimestamp(null)).toBeNull();
    });

    it.each(["2026-02-30T12:00:00Z", "2026-01-01", "infinity", "2026-01-01T00:00:00.1234567Z"])
        ("rejects unsupported timestamps rather than normalizing %s", (value) => {
            expect(() => mysqlUtcTimestamp(value)).toThrow();
        });
});

describe("sensitive source record encryption", () => {
    it("round trips records and uses independent nonces", () => {
        const key = randomBytes(32);
        const record = { status: "verified", fixture: "not a real MFA secret" };
        const first = sealAccountRecord(record, key, "mfa:fixture");
        const second = sealAccountRecord(record, key, "mfa:fixture");
        expect(first).not.toEqual(second);
        expect(openAccountRecord(first, key, "mfa:fixture")).toEqual(record);
        expect(first.includes(Buffer.from(record.fixture))).toBe(false);
    });

    it("rejects swapped records, a wrong key, and modified ciphertext", () => {
        const key = randomBytes(32);
        const encrypted = sealAccountRecord({ value: "test" }, key, "account:first");
        expect(() => openAccountRecord(encrypted, key, "account:second")).toThrow();
        expect(() => openAccountRecord(encrypted, randomBytes(32), "account:first")).toThrow();
        encrypted[encrypted.length - 1] ^= 1;
        expect(() => openAccountRecord(encrypted, key, "account:first")).toThrow();
    });
});

function snapshot() {
    const id = "00000000-0000-4000-8000-000000000001";
    return {
        project: "khjduyjamzwumhducmou",
        tables: {
            "auth.users": [{ id, encrypted_password: "$2a$12$" + "A".repeat(53) }],
            "auth.identities": [],
            "auth.mfa_factors": [{
                id: "00000000-0000-4000-8000-000000000002", user_id: id,
                factor_type: "totp", status: "verified", secret: "A".repeat(32),
            }],
            "public.profiles": [{ user_id: id }],
        },
    };
}

describe("account import preflight", () => {
    it("retains verified MFA and original bcrypt hashes", () => {
        const source = snapshot();
        const records = accountImportRecords(source);
        expect(records.factors[0].status).toBe("verified");
        expect(records.users[0].encrypted_password).toBe(source.tables["auth.users"][0].encrypted_password);
    });

    it("rejects missing users, duplicate accounts, and unsupported hashes", () => {
        const orphan = snapshot();
        orphan.tables["public.profiles"][0].user_id = "missing";
        expect(() => accountImportRecords(orphan)).toThrow("missing source user");
        const duplicate = snapshot();
        duplicate.tables["auth.users"].push(duplicate.tables["auth.users"][0]);
        expect(() => accountImportRecords(duplicate)).toThrow("duplicate");
        const unsupported = snapshot();
        unsupported.tables["auth.users"][0].encrypted_password = "unknown hash";
        expect(() => accountImportRecords(unsupported)).toThrow("Unsupported password");
    });

    it("refuses unsupported MFA rather than silently disabling it", () => {
        const source = snapshot();
        source.tables["auth.mfa_factors"][0].factor_type = "unknown";
        expect(() => accountImportRecords(source)).toThrow("Unsupported MFA");
    });
});
