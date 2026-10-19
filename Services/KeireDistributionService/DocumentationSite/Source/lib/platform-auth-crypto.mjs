import { createHash, createHmac, randomBytes, timingSafeEqual } from "node:crypto";
import bcrypt from "bcryptjs";

export function newOpaqueToken() {
    return randomBytes(32).toString("base64url");
}

export function tokenDigest(token) {
    if (typeof token !== "string" || !/^[A-Za-z0-9_-]{43}$/.test(token)) {
        return null;
    }
    return createHash("sha256").update(token).digest("hex");
}

export async function verifyImportedPassword(password, hash) {
    if (typeof password !== "string" || password.length === 0 || bcrypt.truncates(password) ||
        typeof hash !== "string" || !/^\$2[aby]\$(0[4-9]|1[0-6])\$[./A-Za-z0-9]{53}$/.test(hash)) {
        return false;
    }
    return bcrypt.compare(password, hash);
}

function decodeBase32(secret) {
    if (typeof secret !== "string" || !/^[A-Z2-7]{16,128}$/.test(secret)) {
        throw new Error("Invalid TOTP secret encoding.");
    }
    const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    const bytes = [];
    let accumulator = 0;
    let bits = 0;
    for (const character of secret) {
        accumulator = (accumulator << 5) | alphabet.indexOf(character);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            bytes.push((accumulator >>> bits) & 255);
            accumulator &= (1 << bits) - 1;
        }
    }
    if (accumulator !== 0) {
        throw new Error("Noncanonical TOTP secret encoding.");
    }
    return Buffer.from(bytes);
}

/** Return the accepted counter; the caller must persist it atomically to reject replays. */
export function matchingTotpCounter(secret, code, nowMilliseconds, lastCounter = null, digits = 6) {
    if (![6, 8].includes(digits) || typeof code !== "string" ||
        !new RegExp(`^[0-9]{${digits}}$`).test(code) || !Number.isSafeInteger(nowMilliseconds) ||
        nowMilliseconds < 0 || (lastCounter !== null && (!Number.isSafeInteger(lastCounter) || lastCounter < 0))) {
        return null;
    }
    const key = decodeBase32(secret);
    const currentCounter = Math.floor(nowMilliseconds / 30000);
    const supplied = Buffer.from(code);
    let accepted = null;
    try {
        for (const offset of [-1, 0, 1]) {
            const counter = currentCounter + offset;
            if (counter < 0) {
                continue;
            }
            const message = Buffer.alloc(8);
            message.writeBigUInt64BE(BigInt(counter));
            const digest = createHmac("sha1", key).update(message).digest();
            const position = digest[digest.length - 1] & 15;
            const number = (digest.readUInt32BE(position) & 0x7fffffff) % (10 ** digits);
            const expected = Buffer.from(String(number).padStart(digits, "0"));
            if (timingSafeEqual(expected, supplied) && (lastCounter === null || counter > lastCounter)) {
                accepted = counter;
            }
        }
        return accepted;
    } finally {
        key.fill(0);
    }
}
