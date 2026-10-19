import { describe, expect, it } from "vitest";
import { recoveryBinding } from "./platform-recovery-store.mjs";

const account = { id: "account", email: "qa@invalid.example", password_hash: "password-hash" };
const factor = { id: "factor-a", factor_type: "totp", status: "verified", source_sealed: Buffer.from("encrypted secret") };

describe("password recovery context binding", () => {
    it("invalidates recovery on password, email, or account changes", () => {
        const original = recoveryBinding(account, [factor]);
        for (const field of ["id", "email", "password_hash"]) {
            expect(recoveryBinding({ ...account, [field]: "changed" }, [factor])).not.toBe(original);
        }
    });

    it("invalidates recovery on enrolled factor replacement, removal, or enrollment", () => {
        const original = recoveryBinding(account, [factor]);
        for (const factors of [[], [{ ...factor, status: "unverified" }],
            [{ ...factor, source_sealed: Buffer.from("replacement secret") }],
            [{ ...factor, factor_type: "email" }], [factor, { ...factor, id: "factor-b" }]]) {
            expect(recoveryBinding(account, factors)).not.toBe(original);
        }
    });

    it("does not depend on query order or incomplete enrollment", () => {
        const second = { ...factor, id: "factor-b" };
        expect(recoveryBinding(account, [factor, second])).toBe(recoveryBinding(account, [second, factor]));
        expect(recoveryBinding(account, [factor, { ...second, status: "unverified" }]))
            .toBe(recoveryBinding(account, [factor]));
    });
});
