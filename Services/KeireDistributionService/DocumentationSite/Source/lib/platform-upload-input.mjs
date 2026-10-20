import { createHash } from "node:crypto";

export const maximumPackageBytes = 64 * 1024 * 1024 * 1024;
const semanticVersion = /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$/;
const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
const portableToken = /^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$/;

export class LocalUploadInputError extends Error {
    constructor(field) {
        super(`The package release field ${field} is invalid.`);
        this.status = 400;
        this.code = "publisher.upload_invalid";
    }
}

function text(input, field, minimum, maximum, trim = false) {
    const raw = input[field];
    if (typeof raw !== "string") {
        throw new LocalUploadInputError(field);
    }
    const value = trim ? raw.trim() : raw;
    if (value.length < minimum || value.length > maximum) {
        throw new LocalUploadInputError(field);
    }
    return value;
}

function optionalText(input, field, maximum) {
    return input[field] == null || input[field] === "" ? null : text(input, field, 1, maximum);
}

function identifier(input, field, optional = false) {
    if (optional && (input[field] == null || input[field] === "")) {
        return null;
    }
    const value = text(input, field, 36, 36);
    if (!uuid.test(value)) {
        throw new LocalUploadInputError(field);
    }
    return value.toLowerCase();
}

function tokens(input, field, maximum) {
    const value = input[field];
    if (!Array.isArray(value) || value.length > maximum ||
        value.some((item) => typeof item !== "string" || !portableToken.test(item)) ||
        new Set(value).size !== value.length) {
        throw new LocalUploadInputError(field);
    }
    return [...value];
}

/** Matches the existing publisher upload contract before any database or storage work. */
export function localUploadInput(input) {
    if (!input || typeof input !== "object" || Array.isArray(input)) {
        throw new LocalUploadInputError("request");
    }
    const result = {
        productId: identifier(input, "productId", true),
        publisherId: identifier(input, "publisherId"),
        categoryId: identifier(input, "categoryId"),
        productName: text(input, "productName", 2, 128, true),
        productSummary: text(input, "productSummary", 20, 240, true),
        licenseSpdx: text(input, "licenseSpdx", 2, 64),
        licenseRevision: text(input, "licenseRevision", 1, 64),
        version: text(input, "version", 5, 128),
        minimumEngineVersion: text(input, "minimumEngineVersion", 5, 128),
        maximumEngineVersion: optionalText(input, "maximumEngineVersion", 128),
        installKind: text(input, "installKind", 8, 32),
        platforms: tokens(input, "platforms", 16),
        architectures: tokens(input, "architectures", 16),
        rendererCapabilities: tokens(input, "rendererCapabilities", 32),
        managedApiVersion: optionalText(input, "managedApiVersion", 128),
        releaseNotes: optionalText(input, "releaseNotes", 50000) ?? "",
        expectedSha256: text(input, "expectedSha256", 64, 64),
        expectedSizeBytes: input.expectedSizeBytes,
    };
    for (const field of ["version", "minimumEngineVersion", "maximumEngineVersion"]) {
        if (result[field] !== null && !semanticVersion.test(result[field])) {
            throw new LocalUploadInputError(field);
        }
    }
    if (!["registry", "asset_import", "complete_project"].includes(result.installKind)) {
        throw new LocalUploadInputError("installKind");
    }
    if (!/^[0-9a-f]{64}$/.test(result.expectedSha256)) {
        throw new LocalUploadInputError("expectedSha256");
    }
    if (!Number.isSafeInteger(result.expectedSizeBytes) ||
        result.expectedSizeBytes < 1 || result.expectedSizeBytes > maximumPackageBytes) {
        throw new LocalUploadInputError("expectedSizeBytes");
    }
    return result;
}

/** Preserve the published SQL slug convention so existing drafts are reused, not duplicated. */
export function localUploadProductSlugs(productName) {
    const name = productName.trim();
    const digest = createHash("md5").update(name).digest("hex");
    let slug = name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
    if (slug.length < 2) {
        slug = `asset-${digest.slice(0, 12)}`;
    }
    slug = slug.slice(0, 96).replace(/-+$/g, "");
    return [slug, `${slug.slice(0, 86).replace(/-+$/g, "")}-${digest.slice(0, 8)}`];
}
