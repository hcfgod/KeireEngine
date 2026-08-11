import { access, readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { allDocSources, docAuthorities, docGroups, sourcePathToSlug } from "../doc-library.mjs";

const siteRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repositoryRoot = path.resolve(siteRoot, "..", "..", "..");
const docsRoot = path.join(repositoryRoot, "Docs");
const fallbackLandingPath = path.join(siteRoot, "..", "Website", "docs", "index.html");

async function collectMarkdown(root, directory = root) {
    const results = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const absolute = path.join(directory, entry.name);
        if (entry.isDirectory()) {
            results.push(...await collectMarkdown(root, absolute));
        } else if (entry.isFile() && entry.name.toLowerCase().endsWith(".md")) {
            results.push(path.relative(root, absolute).replaceAll("\\", "/"));
        }
    }
    return results.sort((left, right) => left.localeCompare(right));
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

function markdownAnchors(source) {
    const anchors = new Set();
    const occurrences = new Map();
    for (const match of source.matchAll(/^#{1,6}\s+(.+?)\s*#*\s*$/gm)) {
        const title = match[1]
            .replace(/!?(?:\[([^\]]*)\])\([^)]*\)/g, "$1")
            .replace(/<[^>]+>/g, "")
            .replace(/[`*_~]/g, "")
            .trim()
            .toLowerCase();
        const base = title
            .replace(/[^\p{L}\p{N}\s-]/gu, "")
            .replace(/\s+/g, "-");
        const occurrence = occurrences.get(base) ?? 0;
        occurrences.set(base, occurrence + 1);
        anchors.add(occurrence === 0 ? base : `${base}-${occurrence}`);
    }
    return anchors;
}

const markdownCache = new Map();
async function readMarkdown(absolutePath) {
    if (!markdownCache.has(absolutePath)) {
        markdownCache.set(absolutePath, await readFile(absolutePath, "utf8"));
    }
    return markdownCache.get(absolutePath);
}

async function validateLocalLinks(displayPath, sourceAbsolute, source) {
    const prose = source
        .replace(/^(```|~~~)[\s\S]*?^\1\s*$/gm, "")
        .replace(/`[^`\n]*`/g, "");
    for (const match of prose.matchAll(/!?\[[^\]]*\]\(([^)]+)\)/g)) {
        const rawDestination = match[1].trim().replace(/^<|>$/g, "");
        if (!rawDestination || /^(?:[a-z][a-z0-9+.-]*:|\/)/i.test(rawDestination)) {
            continue;
        }

        const [rawTarget, rawFragment] = rawDestination.split("#", 2);
        const target = decodeURIComponent(rawTarget);
        const fragment = rawFragment ? decodeURIComponent(rawFragment).toLowerCase() : "";
        const targetAbsolute = target
            ? path.resolve(path.dirname(sourceAbsolute), ...target.split("/"))
            : sourceAbsolute;
        await access(targetAbsolute);

        if (fragment && path.extname(targetAbsolute).toLowerCase() === ".md") {
            const anchors = markdownAnchors(await readMarkdown(targetAbsolute));
            assert(anchors.has(fragment), `Broken Markdown fragment in ${displayPath}: ${rawDestination}`);
        }
    }
}

function parseUnsignedConstant(source, name) {
    const match = source.match(new RegExp(`\\b${name}\\s*=\\s*(\\d+)`));
    assert(match, `Could not locate ${name}.`);
    return Number.parseInt(match[1], 10);
}

const actual = await collectMarkdown(docsRoot);
const expected = [...allDocSources].sort((left, right) => left.localeCompare(right));
assert(allDocSources.length === 55, `Expected 55 documentation sources, found ${allDocSources.length}.`);
assert(new Set(allDocSources).size === allDocSources.length, "Documentation inventory contains duplicate source paths.");
assert(JSON.stringify(actual) === JSON.stringify(expected), "Documentation inventory does not exactly cover Docs/**/*.md.");

const slugs = allDocSources.map(sourcePathToSlug);
assert(new Set(slugs).size === slugs.length, "Documentation routes are not unique.");
assert(docGroups.every(({ label, files }) => label && files.length > 0), "Every documentation group must be named and non-empty.");
assert(Object.keys(docAuthorities).length === expected.length && expected.every((sourcePath) => sourcePath in docAuthorities), "Documentation authority inventory must exactly cover every published guide.");

const fallbackLanding = await readFile(fallbackLandingPath, "utf8");
const fallbackRoutes = new Map();
for (const match of fallbackLanding.matchAll(/<a\b[^>]*\bdata-doc-path="Docs\/([^"]+)"[^>]*>/g)) {
    const sourcePath = match[1];
    const href = /\bhref="([^"]+)"/.exec(match[0])?.[1];
    assert(href, `Fallback documentation card has no route: Docs/${sourcePath}`);
    assert(!fallbackRoutes.has(sourcePath), `Fallback documentation has a duplicate card: Docs/${sourcePath}`);
    fallbackRoutes.set(sourcePath, href);
}
assert(fallbackRoutes.size === allDocSources.length, "Fallback documentation does not cover every canonical guide.");
for (const sourcePath of allDocSources) {
    assert(
        fallbackRoutes.get(sourcePath) === `/docs/${sourcePathToSlug(sourcePath)}/`,
        `Fallback documentation route is stale: Docs/${sourcePath}`,
    );
}
assert(
    !/github\.com\/hcfgod\/KeireEngine\/(?:blob|tree)\/master\/Docs/.test(fallbackLanding),
    "Fallback documentation must not redirect guide navigation to GitHub.",
);

for (const sourcePath of allDocSources) {
    const sourceAbsolute = path.join(docsRoot, ...sourcePath.split("/"));
    const source = await readMarkdown(sourceAbsolute);
    assert(/^#\s+\S/m.test(source), `Documentation source has no level-one heading: Docs/${sourcePath}`);
    assert((source.match(/^#\s+\S/gm) ?? []).length === 1, `Documentation source must have exactly one level-one heading: Docs/${sourcePath}`);
    assert(!/\b(?:TODO|TBD)\b/i.test(source), `Documentation source contains an unresolved TODO/TBD marker: Docs/${sourcePath}`);
    await validateLocalLinks(`Docs/${sourcePath}`, sourceAbsolute, source);
    for (const authority of docAuthorities[sourcePath]) {
        await access(path.join(repositoryRoot, ...authority.split("/")));
    }
}

const projectSchema = parseUnsignedConstant(
    await readFile(path.join(repositoryRoot, "KeireCore", "Include", "Keire", "Project", "Project.h"), "utf8"),
    "CurrentProjectSchemaVersion",
);
const sceneSchema = parseUnsignedConstant(
    await readFile(path.join(repositoryRoot, "KeireCore", "Include", "Keire", "Scenes", "SceneAsset.h"), "utf8"),
    "CurrentSceneSchemaVersion",
);
const meshSchema = parseUnsignedConstant(
    await readFile(path.join(repositoryRoot, "KeireCore", "Source", "Assets", "RenderableAssets.cpp"), "utf8"),
    "MeshVersion",
);
const renderingAssetsHeader = await readFile(
    path.join(repositoryRoot, "KeireCore", "Include", "Keire", "Assets", "RenderingAssets.h"),
    "utf8",
);
const materialSchemaMatch = renderingAssetsHeader.match(
    /struct MaterialAssetDefinition[\s\S]*?\bSchemaVersion\s*=\s*(\d+)/,
);
assert(materialSchemaMatch, "Could not locate the material source schema.");
const materialSchema = Number.parseInt(materialSchemaMatch[1], 10);
const vfxSchema = parseUnsignedConstant(
    await readFile(path.join(repositoryRoot, "KeireCore", "Include", "Keire", "Vfx", "VfxSystem.h"), "utf8"),
    "CurrentVfxSchemaVersion",
);
const runtimeSource = await readFile(path.join(repositoryRoot, "KeireRuntime", "Source", "RuntimeApplication.cpp"), "utf8");
const runtimeSchemaMatch = runtimeSource.match(/supported schema:\s*(\d+)/);
assert(runtimeSchemaMatch, "Could not locate the cooked runtime manifest schema.");
const runtimeSchema = Number.parseInt(runtimeSchemaMatch[1], 10);

const versionContracts = [
    ["ProjectSystem.md", new RegExp(`schema version ${projectSchema}\\b`, "i")],
    ["SceneSystem.md", new RegExp(`schema version ${sceneSchema}\\b`, "i")],
    ["ECSAndComponents.md", new RegExp(`scene schema v${sceneSchema}\\b`, "i")],
    ["GameplayFoundations.md", new RegExp(`scene schema v${sceneSchema}\\b`, "i")],
    ["AssetPipeline.md", new RegExp(`\\.keiremesh[^\\n]*version ${meshSchema}\\b`, "i")],
    ["Rendering.md", new RegExp(`mesh schema v${meshSchema}\\b`, "i")],
    ["ShadersAndMaterials.md", new RegExp(`material source schema version ${materialSchema}\\b`, "i")],
    ["Vfx.md", new RegExp(`schema[- ]${vfxSchema}\\b`, "i")],
    ["GameplayFoundations.md", new RegExp(`runtime manifests use schema ${runtimeSchema}\\b`, "i")],
];
for (const [sourcePath, contract] of versionContracts) {
    const source = await readMarkdown(path.join(docsRoot, ...sourcePath.split("/")));
    assert(contract.test(source), `Documentation schema contract is stale: Docs/${sourcePath} (${contract}).`);
}

const rootReadme = await readFile(path.join(repositoryRoot, "README.md"), "utf8");
await validateLocalLinks("README.md", path.join(repositoryRoot, "README.md"), rootReadme);
for (const [label, version] of [
    ["Project descriptor", projectSchema],
    ["Scene source", sceneSchema],
    ["Static mesh", meshSchema],
    ["VFX source", vfxSchema],
    ["Cooked runtime manifest", runtimeSchema],
]) {
    assert(new RegExp(`\\| ${label} \\| ${version} \\|`).test(rootReadme), `Root README has a stale ${label} schema.`);
}

const mermaidSources = await Promise.all(allDocSources.map((sourcePath) => readFile(path.join(docsRoot, ...sourcePath.split("/")), "utf8")));
const mermaidCount = mermaidSources.reduce((count, source) => count + [...source.matchAll(/^```mermaid\s*$/gm)].length, 0);
assert(mermaidCount > 0, "Documentation source must retain Mermaid fences for native GitHub rendering.");

const packageManifest = JSON.parse(await readFile(path.join(siteRoot, "package.json"), "utf8"));
assert(packageManifest.engines?.node === ">=22.12.0", "Documentation build must declare its Node.js production baseline.");
assert(packageManifest.dependencies?.astro && packageManifest.dependencies?.["@astrojs/starlight"] && packageManifest.dependencies?.["beautiful-mermaid"], "Documentation build dependencies are incomplete.");

const config = await readFile(path.join(siteRoot, "astro.config.mjs"), "utf8");
for (const contract of ["base: \"/docs\"", "output: \"static\"", "pagefind: true", "inlineStylesheets: \"never\"", "customCss"]) {
    assert(config.includes(contract), `Documentation configuration is missing ${contract}.`);
}

console.log(`Documentation source validation passed for ${allDocSources.length} canonical guides, ${docGroups.length} navigation groups, ${mermaidCount} Mermaid diagrams, local links, and ${versionContracts.length} schema contracts.`);
