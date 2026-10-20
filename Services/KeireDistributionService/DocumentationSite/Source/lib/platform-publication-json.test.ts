import { describe, expect, it } from "vitest";
import { parsePublicationJson } from "./platform-publication-json.mjs";

describe("publication JSON integer and field integrity", () => {
    it("preserves adjacent integers beyond JavaScript's safe range", () => {
        const parsed = parsePublicationJson(
            '{"sequence":9007199254740993,"signature":{"sequence":9007199254740992}}',
        );
        expect(parsed.sequence).toBe(9007199254740993n);
        expect(parsed.signature.sequence).toBe(9007199254740992n);
        expect(parsed.sequence).not.toBe(parsed.signature.sequence);
    });

    it("preserves the original signed document string including whitespace and escapes", () => {
        const document = '{\r\n  "sequence": 9223372036854775807,\r\n  "path": "a\\\\b"\r\n}';
        const parsed = parsePublicationJson(JSON.stringify({ schemaVersion: 1, document }));
        expect(parsed.document).toBe(document);
        expect(parsePublicationJson(parsed.document).sequence).toBe(9223372036854775807n);
    });

    it.each([
        '{"sequence":1,"sequence":2}',
        '{"signature":{"keyId":"a","key\\u0049d":"b"}}',
        '{"signature":{"sequence":1,"sequence":1}}',
    ])("rejects duplicate decoded fields: %s", (text) => {
        expect(() => parsePublicationJson(text)).toThrow(SyntaxError);
    });

    it.each([
        '{"sequence":01}', '{"sequence":1.0}', '{"sequence":1e3}',
        '{"sequence":true}', '{"sequence":null}', '{"sequence":[]}',
        '{"sequence":1,}', '{"sequence":1} trailing', '{"sequence":}',
        '{"key":"\\q"}', '{"key":"unterminated}', '{"key":"a\nb"}',
        '{"a":{"b":{"c":{"d":{"e":1}}}}}', "1", '"document"', "",
    ])("rejects unsupported or malformed payloads: %s", (text) => {
        expect(() => parsePublicationJson(text)).toThrow(SyntaxError);
    });

    it("has no inherited object fields or prototype mutation", () => {
        const parsed = parsePublicationJson('{"__proto__":{"sequence":1},"constructor":2}');
        expect(Object.getPrototypeOf(parsed)).toBeNull();
        expect(parsed.__proto__.sequence).toBe(1n);
        expect(parsed.constructor).toBe(2n);
    });

    it("bounds input bytes before parsing", () => {
        expect(() => parsePublicationJson(JSON.stringify({ document: "é".repeat(32768) })))
            .toThrow(SyntaxError);
    });
});
