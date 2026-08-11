/**
 * Counts Unicode code points, matching PostgreSQL char_length for UTF-8 text.
 *
 * @param {string} value
 * @returns {number}
 */
export function unicodeLength(value) {
    let length = 0;
    for (const _codePoint of value) {
        ++length;
    }
    return length;
}

/**
 * @param {unknown} value
 * @param {number} maximum
 * @returns {string | null}
 */
export function normalizeText(value, maximum) {
    if (typeof value !== "string") {
        return null;
    }
    const normalized = value.trim();
    if (unicodeLength(normalized) > maximum || normalized.includes("\0")) {
        return null;
    }
    return normalized;
}
