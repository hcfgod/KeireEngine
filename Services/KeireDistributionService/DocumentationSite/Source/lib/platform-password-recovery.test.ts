import { describe, expect, it } from "vitest";
import { validRecoveryPassword } from "./platform-password-recovery.mjs";

describe("local recovered password limits", () => {
    it("rejects short values and nonstrings", () => {
        expect(validRecoveryPassword(null)).toBe(false);
        expect(validRecoveryPassword("short")).toBe(false);
    });

    it("does not silently truncate UTF-8 passwords at bcrypt's byte limit", () => {
        expect(validRecoveryPassword("x".repeat(72))).toBe(true);
        expect(validRecoveryPassword("x".repeat(73))).toBe(false);
        expect(validRecoveryPassword("é".repeat(36))).toBe(true);
        expect(validRecoveryPassword("é".repeat(37))).toBe(false);
    });
});
