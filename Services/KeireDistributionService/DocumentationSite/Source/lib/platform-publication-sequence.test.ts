import { describe, expect, it, vi } from "vitest";
import { allocatePublicationSequence } from "./platform-publication-sequence.mjs";

function fixture(last = "0", job = "0", publications = []) {
    return { execute: vi.fn(async (sql) => {
        if (sql.includes("SELECT CAST(counter_value")) {
            return [[{ counter_value: last }]];
        }
        if (sql.includes("MAX(publication_sequence)")) {
            return [[{ maximum: job }]];
        }
        if (sql.includes("FROM marketplace_publications")) {
            return [publications.map((sequence) => ({ sequence }))];
        }
        return [{ affectedRows: 1 }];
    }) };
}

describe("transactional publication sequence allocation", () => {
    it("starts above both imported jobs and signed publications", async () => {
        const connection = fixture("3", "9", ["12", "5"]);
        expect(await allocatePublicationSequence(connection)).toBe("13");
        expect(connection.execute.mock.calls[1][0]).toContain("FOR UPDATE");
        expect(connection.execute.mock.calls.at(-1)?.[1]).toEqual(["13"]);
    });

    it("preserves exact signed 64-bit values beyond JavaScript number precision", async () => {
        expect(await allocatePublicationSequence(fixture("9007199254740993"))).toBe("9007199254740994");
    });

    it("never lowers an existing counter", async () => {
        expect(await allocatePublicationSequence(fixture("100", "2", ["4"]))).toBe("101");
    });

    it.each(["-1", "1.5", "001", "bad", null, "9223372036854775808"])("rejects invalid imported sequence %s", async (value) => {
        const connection = fixture("0", "0", [value]);
        await expect(allocatePublicationSequence(connection)).rejects.toThrow();
        expect(connection.execute.mock.calls.some(([sql]) => sql.startsWith("UPDATE"))).toBe(false);
    });

    it("fails closed on exhaustion", async () => {
        await expect(allocatePublicationSequence(fixture("9223372036854775807"))).rejects.toThrow("exhausted");
    });
});
