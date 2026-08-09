import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const source = fs.readFileSync(
    path.join(root, "Services", "KeireDistributionService", "Website", "assets", "downloads.js"),
    "utf8",
);

const context = vm.createContext({
    console,
    Date,
    HTMLElement: class HTMLElement {},
    navigator: { platform: "Win32", userAgent: "website-download-test" },
    document: {
        createElement() {
            throw new Error("The catalog validator test must not render elements.");
        },
        querySelector() {
            return null;
        },
        querySelectorAll() {
            return [];
        },
    },
    fetch: async () => ({ ok: false, status: 404 }),
    window: { setTimeout },
});
vm.runInContext(source, context, { filename: "downloads.js" });

const keyId = "ed25519-0123456789abcdef0123456789abcdef";
const digest = "a".repeat(64);

function packageRecord(version, overrides = {}) {
    return {
        schemaVersion: 1,
        packageId: "keire.hub",
        version,
        type: "hubInstaller",
        displayName: `Kéire Hub ${version}`,
        channel: "stable",
        platform: "windows",
        architecture: "x86_64",
        signatureKeyId: keyId,
        artifact: { sizeBytes: 4096, sha256: digest },
        installedSizeBytes: 4096,
        files: [{ path: "KeireHubSetup.exe", sizeBytes: 4096, sha256: digest, mode: 420 }],
        ...overrides,
    };
}

function catalog(packages, overrides = {}) {
    return {
        schemaVersion: 1,
        keyId,
        sequence: 42,
        expiresAt: "2099-01-01T00:00:00+00:00",
        channel: "stable",
        platform: "windows",
        architecture: "x86_64",
        packages,
        ...overrides,
    };
}

function validate(document) {
    context.fixture = document;
    return vm.runInContext("validateCatalog(fixture, 'windows', 'x86_64')", context);
}

const ordered = validate(catalog([
    packageRecord("2.0.0-beta.1"),
    packageRecord("1.999999999999999999999999.0"),
    packageRecord("2.0.0"),
]));
assert.equal(ordered.length, 3);
assert.equal(ordered[0].version.raw, "2.0.0");
assert.equal(ordered[1].version.raw, "2.0.0-beta.1");

const compact = packageRecord("2.1.0", {
    manifest: { sizeBytes: 1024, sha256: "c".repeat(64) },
});
delete compact.files;
const compactCandidates = validate(catalog([compact], { schemaVersion: 2 }));
assert.equal(compactCandidates.length, 1);
assert.equal(compactCandidates[0].installerName, "KeireHubSetup.exe");
assert.equal(validate(catalog([packageRecord("2.1.0")], { schemaVersion: 2 })).length, 0);

assert.equal(validate(catalog([packageRecord("1.2.3")], { expiresAt: "2020-01-01T00:00:00Z" })).length, 0);
assert.equal(validate(catalog([packageRecord("1.2.3")], { keyId: "release-key" })).length, 0);
assert.equal(validate(catalog([packageRecord("1.2.3")], { platform: "linux" })).length, 0);
assert.equal(validate(catalog([packageRecord("1.2.3-01")])).length, 0);
assert.equal(validate(catalog([packageRecord("1.2.3", {
    files: [{ path: "KeireHubSetup.exe", sizeBytes: 4096, sha256: "b".repeat(64), mode: 420 }],
})])).length, 0);
assert.equal(validate(catalog([packageRecord("1.2.3", {
    files: [{ path: "../KeireHubSetup.exe", sizeBytes: 4096, sha256: digest, mode: 420 }],
})])).length, 0);

function validatePreview(document) {
    context.fixture = document;
    return vm.runInContext("validatePreviewMetadata(fixture)", context);
}

const preview = {
    schemaVersion: 2,
    packages: [{
        type: "hubInstallerPreview",
        releaseId: "windows-x86_64-0.1.0-20260808.1",
        version: "0.1.0",
        editorVersion: "0.1.0",
        publishedAt: "2026-08-08T01:06:57Z",
        platform: "windows",
        architecture: "x86_64",
        fileName: "keire-hub-windows-x86_64-0.1.0-preview-aaaaaaaa.exe",
        url: "/preview-downloads/keire-hub-windows-x86_64-0.1.0-preview-aaaaaaaa.exe",
        sizeBytes: 4096,
        sha256: digest,
        signed: false,
        developmentArtifact: true,
    }],
};
assert.equal(validatePreview(preview).length, 1);
assert.equal(validatePreview({ ...preview, packages: [{ ...preview.packages[0], signed: true }] }).length, 0);
assert.equal(validatePreview({ ...preview, packages: [{ ...preview.packages[0], editorVersion: "latest" }] }).length, 0);
assert.equal(validatePreview({ ...preview, packages: [{ ...preview.packages[0], releaseId: "../preview" }] }).length, 0);
assert.equal(validatePreview({ ...preview, packages: [{ ...preview.packages[0], url: "https://example.com/setup.exe" }] }).length, 0);
assert.equal(validatePreview({ ...preview, packages: [{ ...preview.packages[0], fileName: "../setup.exe" }] }).length, 0);

await new Promise((resolve) => setTimeout(resolve, 0));

class RenderElement {
    constructor(name = "div") {
        this.name = name;
        this.attributes = new Map();
        this.children = [];
        this.className = "";
        this.classList = { add: (value) => { this.addedClass = value; } };
        this.textContent = "";
    }

    append(...children) {
        this.children.push(...children);
    }

    addEventListener() {}

    setAttribute(name, value) {
        this.attributes.set(name, value);
    }
}

const cards = new Map();
for (const platform of ["windows", "macos", "linux"]) {
    const card = new RenderElement("article");
    card.state = new RenderElement("p");
    card.variants = new RenderElement("div");
    card.querySelector = (selector) => selector === "[data-download-state]" ? card.state : card.variants;
    cards.set(platform, card);
}
const renderContext = vm.createContext({
    console,
    Date,
    HTMLElement: RenderElement,
    navigator: { platform: "Win32", userAgent: "website-download-render-test", clipboard: { writeText: async () => {} } },
    document: {
        createElement(name) {
            return new RenderElement(name);
        },
        querySelector(selector) {
            const match = /data-platform="([a-z]+)"/.exec(selector);
            return match ? cards.get(match[1]) : null;
        },
        querySelectorAll() {
            return [];
        },
    },
    fetch: async (url, options = {}) => {
        if (url === "/assets/preview-downloads.json") {
            return { ok: true, status: 200, json: async () => preview };
        }
        if (url === preview.packages[0].url && options.method === "HEAD") {
            return { ok: true, status: 200, headers: { get: () => "4096" } };
        }
        return { ok: false, status: 404 };
    },
    window: { setTimeout },
});
vm.runInContext(source, renderContext, { filename: "downloads-render.js" });
for (let index = 0; index < 10 && cards.get("windows").variants.children.length === 0; ++index) {
    await new Promise((resolve) => setTimeout(resolve, 0));
}
const renderedVariant = cards.get("windows").variants.children[0];
assert.ok(renderedVariant);
assert.equal(renderedVariant.className, "download-variant preview-variant");
assert.match(renderedVariant.children[2].textContent, /Editor v0\.1\.0/);
assert.equal(renderedVariant.children[3].name, "a");
assert.equal(renderedVariant.children[3].href, preview.packages[0].url);
assert.equal(renderedVariant.children[3].download, preview.packages[0].fileName);
assert.equal(cards.get("windows").addedClass, "recommended");
assert.match(cards.get("windows").state.textContent, /Development preview available/);

const historyTargets = new Map();
const historyStates = new Map();
for (const platform of ["windows", "macos", "linux"]) {
    historyTargets.set(platform, new RenderElement("div"));
    historyStates.set(platform, new RenderElement("span"));
}
const historyContext = vm.createContext({
    console,
    Date,
    Intl,
    HTMLElement: RenderElement,
    navigator: { platform: "Win32", userAgent: "website-download-history-test", clipboard: { writeText: async () => {} } },
    document: {
        createElement(name) {
            return new RenderElement(name);
        },
        querySelector(selector) {
            if (selector === "[data-download-history]") {
                return new RenderElement("main");
            }
            const platform = /data-history-platform="([a-z]+)"/.exec(selector);
            if (platform) {
                return historyTargets.get(platform[1]);
            }
            const state = /data-history-state="([a-z]+)"/.exec(selector);
            return state ? historyStates.get(state[1]) : null;
        },
        querySelectorAll() {
            return [];
        },
    },
    fetch: async (url, options = {}) => {
        if (url === "/assets/preview-downloads.json") {
            return { ok: true, status: 200, json: async () => preview };
        }
        if (url === preview.packages[0].url && options.method === "HEAD") {
            return { ok: true, status: 200, headers: { get: () => "4096" } };
        }
        return { ok: false, status: 404 };
    },
    window: { setTimeout },
});
vm.runInContext(source, historyContext, { filename: "downloads-history.js" });
for (let index = 0; index < 10 && historyTargets.get("windows").children.length === 0; ++index) {
    await new Promise((resolve) => setTimeout(resolve, 0));
}
assert.equal(historyTargets.get("windows").children.length, 1);
assert.match(historyStates.get("windows").textContent, /1 retained release/);
assert.match(historyStates.get("linux").textContent, /No retained releases/);

console.log("Website download catalog validation passed.");
