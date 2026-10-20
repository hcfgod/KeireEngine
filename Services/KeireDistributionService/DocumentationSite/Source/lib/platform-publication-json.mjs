const maximumDocumentBytes = 64 * 1024;
const maximumDepth = 4;

/**
 * Publication envelopes contain only objects, strings and integer numbers.
 * Preserve integer tokens as bigint and reject duplicate keys in every object.
 * Never reserialize this result to obtain the bytes used for signature verification.
 */
export function parsePublicationJson(text) {
    if (typeof text !== "string" || Buffer.byteLength(text, "utf8") > maximumDocumentBytes) {
        throw new SyntaxError("The publication document is missing or too large.");
    }
    let position = 0;

    function invalid() {
        throw new SyntaxError("The publication document has an invalid or ambiguous JSON structure.");
    }

    function skipWhitespace() {
        while (position < text.length && /[ \t\r\n]/.test(text[position])) {
            position++;
        }
    }

    function readString() {
        const start = position++;
        while (position < text.length) {
            const character = text[position++];
            if (character === "\\") {
                position++;
            } else if (character === '"') {
                return JSON.parse(text.slice(start, position));
            }
        }
        return invalid();
    }

    function readValue(depth) {
        skipWhitespace();
        if (depth > maximumDepth) {
            return invalid();
        }
        if (text[position] === '"') {
            return readString();
        }
        if (text[position] === "{") {
            position++;
            const result = Object.create(null);
            skipWhitespace();
            if (text[position] === "}") {
                position++;
                return result;
            }
            while (position < text.length) {
                skipWhitespace();
                if (text[position] !== '"') {
                    return invalid();
                }
                const key = readString();
                if (Object.hasOwn(result, key)) {
                    return invalid();
                }
                skipWhitespace();
                if (text[position++] !== ":") {
                    return invalid();
                }
                result[key] = readValue(depth + 1);
                skipWhitespace();
                const separator = text[position++];
                if (separator === "}") {
                    return result;
                }
                if (separator !== ",") {
                    return invalid();
                }
            }
            return invalid();
        }
        const integer = /^-?(?:0|[1-9][0-9]*)/.exec(text.slice(position));
        if (!integer) {
            return invalid();
        }
        position += integer[0].length;
        return BigInt(integer[0]);
    }

    const result = readValue(0);
    skipWhitespace();
    if (position !== text.length || result === null || typeof result !== "object") {
        return invalid();
    }
    return result;
}
