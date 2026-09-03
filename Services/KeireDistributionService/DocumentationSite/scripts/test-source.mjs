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

function stableVersionParts(value) {
    const match = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.exec(value);
    return match ? match.slice(1).map(Number) : null;
}

function olderStableVersion(left, right) {
    const leftParts = stableVersionParts(left);
    const rightParts = stableVersionParts(right);
    if (!leftParts || !rightParts) {
        return false;
    }
    for (let index = 0; index < leftParts.length; ++index) {
        if (leftParts[index] !== rightParts[index]) {
            return leftParts[index] < rightParts[index];
        }
    }
    return false;
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
assert(allDocSources.length === 86, `Expected 86 documentation sources, found ${allDocSources.length}.`);
assert(new Set(allDocSources).size === allDocSources.length, "Documentation inventory contains duplicate source paths.");
assert(JSON.stringify(actual) === JSON.stringify(expected), "Documentation inventory does not exactly cover Docs/**/*.md.");

const slugs = allDocSources.map(sourcePathToSlug);
assert(new Set(slugs).size === slugs.length, "Documentation routes are not unique.");
assert(docGroups.every(({ label, files }) => label && files.length > 0), "Every documentation group must be named and non-empty.");
assert(Object.keys(docAuthorities).length === expected.length && expected.every((sourcePath) => sourcePath in docAuthorities), "Documentation authority inventory must exactly cover every published guide.");

const documentationOverview = await readMarkdown(path.join(docsRoot, "README.md"));
const documentedGuideCount = /All (\d+) published guides/.exec(documentationOverview);
assert(documentedGuideCount && Number.parseInt(documentedGuideCount[1], 10) === allDocSources.length,
    "Documentation overview guide count does not match the canonical inventory.");
const projectConfiguration = await readFile(path.join(repositoryRoot, "Config", "Project.conf"), "utf8");
const projectVersion = /^PROJECT_VERSION=(\S+)$/m.exec(projectConfiguration)?.[1];
assert(projectVersion && documentationOverview.includes(`currently version ${projectVersion}`),
    "Documentation overview version does not match Config/Project.conf.");
assert(!documentationOverview.includes("npm run test:search"),
    "Documentation overview references the removed standalone search-test script.");

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
assert(fallbackLanding.includes(`<span data-doc-count>${allDocSources.length} documents</span>`),
    "Fallback documentation count is stale.");
for (const [fragment, count] of [
    ["user-manual", 18], ["getting-projects", 5], ["editor-authoring", 11], ["engine-systems", 13], ["assets-builds", 4],
    ["vfx", 4], ["csharp", 16], ["production", 8], ["diagnostics", 5],
]) {
    assert(new RegExp(`<a href="#${fragment}">[^<]*<span>[^<]+</span><b>${count}</b></a>`).test(fallbackLanding),
    `Fallback documentation category count is stale: ${fragment}.`);
}

const manualSources = allDocSources.filter((sourcePath) => sourcePath.startsWith("Manual/"));
assert(manualSources.length === 18, `Expected 18 user-manual guides, found ${manualSources.length}.`);
for (const topic of ["ShaderGraph.md", "MaterialGraph.md", "VfxGraph.md", "GraphEditing.md",
    "PlayerBuildsAndPackages.md", "CSharpApiQuickReference.md", "VisualWorkflowMaps.md", "ScriptingRecipes.md",
    "ShaderGraphExamples.md", "MaterialGraphExamples.md", "VfxGraphExamples.md"]) {
    assert(manualSources.includes(`Manual/${topic}`), `User manual is missing ${topic}.`);
}
await access(path.join(docsRoot, "Manual", "Examples", "Keire.ManualExamples.csproj"));
await access(path.join(docsRoot, "Manual", "Examples", "ManualExamples.cs"));
const manualOverview = await readMarkdown(path.join(docsRoot, "Manual", "README.md"));
assert(manualOverview.includes("compile-checkable example project"),
    "User manual does not link its compile-checked C# examples.");

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
const animatorSchema = parseUnsignedConstant(
    await readFile(path.join(repositoryRoot, "KeireCore", "Source", "ECS", "Components", "AnimatorComponent.cpp"), "utf8"),
    "AnimatorSchemaVersion",
);
const proceduralMotionHeader = await readFile(
    path.join(repositoryRoot, "KeireCore", "Include", "Keire", "Animation", "ProceduralMotion.h"),
    "utf8",
);
const proceduralMotionSchemaMatch = proceduralMotionHeader.match(
    /struct ProceduralMotionProfile[\s\S]*?\bSchemaVersion\s*=\s*(\d+)/,
);
assert(proceduralMotionSchemaMatch, "Could not locate the procedural motion profile schema.");
const proceduralMotionSchema = Number.parseInt(proceduralMotionSchemaMatch[1], 10);
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
    ["AnimationRigging.md", new RegExp(`Animator component schema ${animatorSchema}\\b`, "i")],
    ["ProceduralMotion.md", new RegExp(`\\.keiremotionprofile[^\\n]*schema version ${proceduralMotionSchema}\\b`, "i")],
    ["GameplayFoundations.md", new RegExp(`runtime manifests use schema ${runtimeSchema}\\b`, "i")],
];
for (const [sourcePath, contract] of versionContracts) {
    const source = await readMarkdown(path.join(docsRoot, ...sourcePath.split("/")));
    assert(contract.test(source), `Documentation schema contract is stale: Docs/${sourcePath} (${contract}).`);
}

const assetPipelineGuide = await readMarkdown(path.join(docsRoot, "AssetPipeline.md"));
assert(assetPipelineGuide.includes(`## Static mesh version ${meshSchema}`),
    "Asset Pipeline heading does not identify the current mesh version.");
const renderingGuide = await readMarkdown(path.join(docsRoot, "Rendering.md"));
assert(renderingGuide.includes(`Material schema v${materialSchema} owns`) &&
    !renderingGuide.includes("authored transparency requires a future explicit surface mode"),
    "Rendering guide contains a stale material schema or transparency claim.");

const materialMatrix = await readMarkdown(path.join(docsRoot, "MaterialParityMatrix.md"));
const materialRows = [...materialMatrix.matchAll(/^\| ME-[A-Z]{2}-\d{3} \|.*?\| (Complete|Partial|Planned) \|/gm)];
const materialCounts = { Complete: 0, Partial: 0, Planned: 0 };
for (const row of materialRows) {
    materialCounts[row[1]] += 1;
}
assert(materialRows.length === 145, `Expected 145 material capability rows, found ${materialRows.length}.`);
const materialSummary = `${materialRows.length} rows — ${materialCounts.Complete} Complete, ` +
    `${materialCounts.Partial} Partial, and ${materialCounts.Planned} Planned`;
assert(materialMatrix.includes(materialSummary), "Material capability summary does not match its rows.");

const productionReview = await readMarkdown(path.join(docsRoot, "ProductionReadinessReview.md"));
assert(productionReview.includes(`contains ${allDocSources.length} guides`),
    "Production-readiness documentation count is stale.");
assert(productionReview.includes(`${materialRows.length} rows: ${materialCounts.Complete} Complete, ` +
    `${materialCounts.Partial} Partial, and ${materialCounts.Planned} Planned`),
    "Production-readiness material counts do not match the capability matrix.");
const readinessReviewDate = /^Review date: (\d{4}-\d{2}-\d{2})$/m.exec(productionReview)?.[1];
assert(readinessReviewDate, "Production-readiness review date is missing or invalid.");
const readinessAuditLabel = `Audited ${new Intl.DateTimeFormat("en-GB", {
    day: "numeric", month: "short", year: "numeric", timeZone: "UTC",
}).format(new Date(`${readinessReviewDate}T00:00:00Z`))}`;
const readinessScoreRows = [...productionReview.matchAll(/^\| [^|]+ \| (\d+)% \| [A-C][+-]? \| (\d+)\/100 \|/gm)];
assert(readinessScoreRows.length === 10, "Production-readiness weighted rubric must contain ten scored domains.");
const readinessWeight = readinessScoreRows.reduce((sum, row) => sum + Number.parseInt(row[1], 10), 0);
const readinessWeightedScore = Math.round(readinessScoreRows.reduce((sum, row) =>
    sum + Number.parseInt(row[1], 10) * Number.parseInt(row[2], 10), 0) / readinessWeight);
assert(readinessWeight === 100 &&
    productionReview.includes(`| Weighted audited 0.4.1 readiness | **100%** | **A-** | **${readinessWeightedScore}/100** |`),
    "Production-readiness overall score does not match its weighted domain scores.");

const vfxManifest = JSON.parse(await readFile(path.join(docsRoot, "VfxParityManifest.json"), "utf8"));
const vfxImplemented = vfxManifest.counts?.WithKeireImplementation;
const vfxDisabled = vfxManifest.counts?.Disabled;
const vfxTotal = vfxManifest.counts?.Total;
assert(Number.isInteger(vfxImplemented) && Number.isInteger(vfxDisabled) && vfxTotal === vfxImplemented + vfxDisabled,
    "VFX parity manifest counts are invalid.");
assert(productionReview.includes(`${vfxImplemented} implemented and ${vfxDisabled} disabled`) &&
    productionReview.includes(`cover ${vfxImplemented}/${vfxTotal} rows`),
    "Production-readiness VFX counts do not match the parity manifest.");
const visualInitiatives = await readMarkdown(path.join(docsRoot, "VisualAuthoringInitiatives.md"));
assert(visualInitiatives.includes(`${vfxImplemented} enabled Kéire-equivalent rows, ${vfxDisabled} disabled rows`) &&
    visualInitiatives.includes("Schema-v5 JSON") && visualInitiatives.includes("Twelve paired Sandbox"),
    "Visual-authoring baseline is stale.");

const managedMatrix = await readMarkdown(path.join(docsRoot, "Scripting", "ManagedApiMatrix.md"));
for (const staleClaim of ["Multi-scene query scopes", "Unified additive worlds", "add native readiness for"]) {
    assert(!managedMatrix.includes(staleClaim), `Managed API matrix contains a completed feature as future work: ${staleClaim}.`);
}
const assetPackagesGuide = await readMarkdown(path.join(docsRoot, "AssetPackages.md"));
assert(!assetPackagesGuide.includes("prepare-marketplace-publication.ps1"),
    "Asset Packages guide references the retired browser-envelope publication workflow.");
const vfxGuide = await readMarkdown(path.join(docsRoot, "Vfx.md"));
for (const sourcePath of ["RenderVfxRecording.cpp", "RenderVfxPipelines.cpp", "RenderVfxDrawing.cpp", "SceneRuntimeVfx.cpp"]) {
    assert(vfxGuide.includes(sourcePath), `VFX implementation map is missing ${sourcePath}.`);
}

const rootReadme = await readFile(path.join(repositoryRoot, "README.md"), "utf8");
await validateLocalLinks("README.md", path.join(repositoryRoot, "README.md"), rootReadme);
assert(rootReadme.includes(`currently **version ${projectVersion}`),
    "Root README version does not match Config/Project.conf.");
assert(rootReadme.includes(`contains ${allDocSources.length} maintained guides`),
    "Root README guide count does not match the canonical inventory.");
const documentationLanding = await readFile(path.join(siteRoot, "Source", "content", "docs", "index.mdx"), "utf8");
assert(documentationLanding.includes(`${allDocSources.length} maintained guides`),
    "Documentation website landing guide count does not match the canonical inventory.");
for (const sourcePath of ["GettingStarted.md", "TestingAndRelease.md", "ProjectHub.md", "ProductionReadinessReview.md"]) {
    const source = await readMarkdown(path.join(docsRoot, sourcePath));
    assert(source.includes(projectVersion), `Current release guide does not mention ${projectVersion}: Docs/${sourcePath}`);
    assert(!/active (?:Windows )?0\.3\.1 stable (?:catalog|snapshot)/i.test(source),
        `Current release guide still describes the 0.3.1 distribution as active: Docs/${sourcePath}`);
}
const previewDownloadMetadata = JSON.parse(await readFile(path.join(repositoryRoot, "Services",
    "KeireDistributionService", "Website", "assets", "preview-downloads.json"), "utf8"));
const releaseStatus = previewDownloadMetadata?.releaseStatus;
const activeCatalogVersion = releaseStatus?.activeCatalogVersion;
const downloadsPage = await readFile(path.join(siteRoot, "Source", "pages", "downloads", "index.astro"), "utf8");
for (const contract of [
    `Kéire ${projectVersion} is the current Windows release target`,
    "Package links are populated exclusively from the active, signed distribution catalog",
    `Signed ${activeCatalogVersion} Windows and Linux x86-64 packages remain active`,
    "a Windows-only activation will retain Linux 0.4.2 instead of hiding it",
    "Every link shown here has an active catalog record and verified artifact hash",
    "Use DEB on Ubuntu or Debian and RPM on Rocky Linux, Fedora, or openSUSE",
    "The EXE is not yet Authenticode-signed",
    "Hub 0.4.1 cannot launch an unsigned 0.4.2 installer through in-app Update",
    "existing 0.4.1 users must download and run 0.4.2 manually",
]) {
    assert(downloadsPage.includes(contract), `Downloads page is missing current platform contract: ${contract}`);
}
const windowsDownloadsPage = await readFile(path.join(siteRoot, "Source", "pages", "downloads", "windows", "index.astro"), "utf8");
for (const contract of [
    "The current EXE is not Authenticode-signed",
    "unknown-publisher warning",
    "Verify the displayed SHA-256",
    "The 0.4.1 in-app updater requires an Authenticode signature",
    "Download and run 0.4.2 manually",
    "<dt>Native signing</dt><dd>Not Authenticode-signed</dd>",
]) {
    assert(windowsDownloadsPage.includes(contract),
        `Windows downloads page is missing unsigned-installer guidance: ${contract}`);
}
assert(releaseStatus?.version === projectVersion && ["preparing", "active"].includes(releaseStatus.state),
    "Download fallback metadata must identify the current release and its publication state.");
if (releaseStatus.state === "active") {
    assert(releaseStatus.activeCatalogVersion === projectVersion,
        "An active download fallback must identify the current signed catalog version.");
} else {
    assert(olderStableVersion(activeCatalogVersion, projectVersion) && previewDownloadMetadata.packages?.length === 0 &&
        /catalog|activation/i.test(releaseStatus.message),
        "A preparing download fallback must not invent packages and must explain catalog activation.");
}
for (const [displayPath, source] of [
    ["README.md", rootReadme],
    ["Docs/README.md", documentationOverview],
    ["Docs/GettingStarted.md", await readMarkdown(path.join(docsRoot, "GettingStarted.md"))],
    ["Docs/TestingAndRelease.md", await readMarkdown(path.join(docsRoot, "TestingAndRelease.md"))],
]) {
    assert(!source.includes("/downloads/previous/"), `${displayPath} references the retired download archive route.`);
}
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
const packageLock = JSON.parse(await readFile(path.join(siteRoot, "package-lock.json"), "utf8"));
assert(packageManifest.version === projectVersion && packageLock.version === projectVersion &&
    packageLock.packages?.[""]?.version === projectVersion,
    "Documentation package metadata and the root lockfile entry must match Config/Project.conf.");
assert(packageManifest.engines?.node === ">=22.12.0", "Documentation build must declare its Node.js production baseline.");
assert(
    packageManifest.dependencies?.astro
        && packageManifest.dependencies?.["@astrojs/node"]
        && packageManifest.dependencies?.["@astrojs/starlight"]
        && packageManifest.dependencies?.["@supabase/ssr"]
        && packageManifest.dependencies?.["beautiful-mermaid"]
        && packageManifest.dependencies?.["@noble/hashes"]
        && packageManifest.dependencies?.["tus-js-client"],
    "Unified web-platform build dependencies are incomplete.",
);

const config = await readFile(path.join(siteRoot, "astro.config.mjs"), "utf8");
for (const contract of ["output: \"server\"", "adapter: node", "pagefind: true", "inlineStylesheets: \"never\"", "customCss"]) {
    assert(config.includes(contract), `Documentation configuration is missing ${contract}.`);
}
assert(config.includes("checkOrigin: false") && config.includes("Source/middleware.ts"),
    "Astro's loopback origin check must defer to the documented proxy-aware mutation guard.");

const middleware = await readFile(path.join(siteRoot, "Source", "middleware.ts"), "utf8");
const healthRoute = await readFile(path.join(siteRoot, "Source", "pages", "health", "index.ts"), "utf8");
const contactRoute = await readFile(path.join(siteRoot, "Source", "pages", "contact", "submit.ts"), "utf8");
assert(healthRoute.includes(`version: "${projectVersion}"`) &&
    healthRoute.includes(`releaseState: "${releaseStatus.state === "active" ? "current" : "preparing"}"`) &&
    healthRoute.includes(`targetCatalogVersion: "${projectVersion}"`) &&
    healthRoute.includes(`catalogState: "${releaseStatus.state}"`) &&
    healthRoute.includes(`activeCatalogVersion: "${activeCatalogVersion}"`),
    "Health metadata must identify the current release target and the separately verified active catalog.");
for (const source of [middleware, healthRoute, contactRoute]) {
    assert(source.includes("runtimeEnvironment("), "SSR routes must read deploy-time settings from the Node process.");
    assert(!/import\.meta\.env\.(?:PUBLIC_SUPABASE|KEIRE_)/.test(source), "SSR routes contain a build-time deployment setting.");
}
assert(healthRoute.includes("export const HEAD") && healthRoute.includes("status: 204"),
    "The web process must expose a dependency-free liveness probe for Caddy.");
assert(middleware.includes('"x-forwarded-proto"') && middleware.includes('"x-forwarded-host"'),
    "Same-origin checks must account for the trusted loopback reverse proxy.");
assert(middleware.includes('runtimeEnvironment("PUBLIC_SITE_URL")') &&
    middleware.includes('configuredUrl.protocol !== "https:"') &&
    middleware.includes("new URL(origin).origin === expectedOrigin"),
    "Browser mutations must be checked against the canonical HTTPS deployment origin.");
assert(middleware.includes('new Set(["POST", "PUT", "PATCH", "DELETE"])') &&
    middleware.includes("isMutation && !bearerToken && !isSameOrigin(context.request)"),
    "Every cookie-authenticated mutation method must pass the same-origin guard.");
assert(middleware.includes('parseCookieHeader(context.request.headers.get("cookie")'),
    "Astro SSR must parse request cookies through the supported request-header adapter.");
assert(middleware.includes('context.request.method === "HEAD"') &&
    middleware.includes('context.url.pathname.replace(/\\/+$/, "") === "/health"') &&
    middleware.indexOf('context.request.method === "HEAD"') < middleware.indexOf("createServerClient("),
    "Caddy's exact Astro liveness probe must return before authentication or external dependency work.");
assert(middleware.includes("setAll: (cookies, headers)") && middleware.includes("authResponseHeaders.set") &&
    middleware.includes("for (const [name, value] of authResponseHeaders)"),
    "Astro SSR must preserve Supabase auth cache headers alongside refreshed cookies.");
assert(middleware.includes("appendPkceFlowIdToRedirects: true"),
    "Astro SSR must correlate concurrent PKCE callbacks with their originating verifier.");
for (const contract of ["getAssuranceState", "context.locals.assurance"]) {
    assert(middleware.includes(contract), `Account middleware is missing MFA contract ${contract}.`);
}
const authLibrary = await readFile(path.join(siteRoot, "Source", "lib", "auth.ts"), "utf8");
assert(authLibrary.includes("getAuthenticatorAssuranceLevel"),
    "The account assurance helper must use Supabase's verified MFA assurance API.");
assert(authLibrary.includes('runtimeEnvironment("PUBLIC_SITE_URL")') && authLibrary.includes('origin.protocol !== "https:"'),
    "Account redirect URLs must use the explicit canonical HTTPS origin.");
assert(authLibrary.includes("oauthFailurePath") && authLibrary.includes('oauth_error: reason'),
    "OAuth callback failures must use the bounded human-facing recovery route.");
assert(authLibrary.includes('value.includes("\\\\")') && authLibrary.includes("resolved.origin === expectedOrigin"),
    "Account redirects must reject backslash-normalized and cross-origin paths.");
for (const relativePath of [
    "Source/pages/account/github/index.ts",
    "Source/pages/account/recovery/index.ts",
    "Source/pages/account/registration/index.ts",
]) {
    const redirectSource = await readFile(path.join(siteRoot, ...relativePath.split("/")), "utf8");
    assert(redirectSource.includes("externalUrl("), `Account redirect route bypasses the canonical origin: ${relativePath}.`);
    assert(!redirectSource.includes("context.url).toString()"), `Account redirect route trusts the loopback origin: ${relativePath}.`);
}
const accountSources = await Promise.all([
    "Source/pages/account/mfa/index.astro",
    "Source/pages/account/mfa/enroll/index.ts",
    "Source/pages/account/mfa/verify/index.ts",
    "Source/pages/account/mfa/remove/index.ts",
    "Source/pages/account/mfa/challenge/index.astro",
    "Source/pages/account/github/index.ts",
].map((relativePath) => readFile(path.join(siteRoot, ...relativePath.split("/")), "utf8")));
const accountContracts = accountSources.join("\n");
for (const contract of [
    "auth.mfa.enroll",
    "auth.mfa.challengeAndVerify",
    "auth.mfa.unenroll",
    "auth.mfa.listFactors",
    "auth.linkIdentity",
]) {
    assert(accountContracts.includes(contract), `Account security workflow is missing ${contract}.`);
}
assert(accountContracts.includes('pattern="[0-9]{6}"'), "MFA forms must constrain authenticator codes.");
const oauthCallback = await readFile(path.join(siteRoot, "Source", "pages", "account", "callback", "index.ts"), "utf8");
assert(oauthCallback.includes('searchParams.get("sb_flow_id")') && oauthCallback.includes("flowId ? { flowId } : undefined"),
    "OAuth callbacks must exchange each code with its matching PKCE flow verifier.");
assert(oauthCallback.includes('return fail("exchange_failed")') && !oauthCallback.includes("return apiError("),
    "OAuth callback failures must not expose raw API errors in the browser.");
const hubOAuthCallback = await readFile(
    path.join(siteRoot, "Source", "pages", "oauth", "hub", "callback.astro"),
    "utf8",
);
for (const contract of [
    "keirehub://oauth/callback?",
    "data-hub-handoff",
    "data-hub-handoff-status",
    "data-hub-handoff-recovery",
    'window.addEventListener("blur"',
    'document.addEventListener("visibilitychange"',
    'href="/downloads/"',
]) {
    assert(hubOAuthCallback.includes(contract), `Hub OAuth callback recovery is missing ${contract}.`);
}
assert(hubOAuthCallback.includes("This page forwards only the bounded authorization response") &&
    !hubOAuthCallback.includes("navigator.clipboard"),
    "Hub OAuth recovery must not expose the single-use authorization code for copying.");
const signInPage = await readFile(path.join(siteRoot, "Source", "pages", "account", "sign-in.astro"), "utf8");
assert(signInPage.includes('role="alert"') && signInPage.includes("finish in the same browser"),
    "The sign-in page must provide an accessible PKCE recovery message.");
assert(signInPage.includes("safeLocalPath(Astro.url.searchParams.get"),
    "The sign-in page must use the shared hardened local redirect validator.");
const accountSessionRoute = await readFile(path.join(siteRoot, "Source", "pages", "account", "session", "index.ts"), "utf8");
assert(accountSessionRoute.includes("export const GET") && accountSessionRoute.includes("signedIn: Boolean(context.locals.user)"),
    "The website must expose a no-store session status used to repair stale browser-cache navigation state.");
const accountLink = await readFile(path.join(siteRoot, "Source", "components", "SessionAwareAccountLink.astro"), "utf8");
assert(accountLink.includes('window.addEventListener("pageshow"') && accountLink.includes("event.persisted") &&
    accountLink.includes('cache: "no-store"'),
    "Account navigation must refresh signed-in state when the browser restores a cached page.");
assert(accountLink.includes('localStorage.setItem(accountStateStorageKey') && accountLink.includes("restoreAccountLinks()") &&
    accountLink.includes('data-session-server-signed-in={String(signedIn)}'),
    "Account navigation must restore its last verified presentation without overriding server-confirmed sign-in.");
assert(accountLink.includes("<script is:inline>") && accountLink.includes("document.currentScript") &&
    accountLink.includes('state = "signed-in"'),
    "Account navigation must restore verified presentation state before deferred page scripts run.");
const platformHeader = await readFile(path.join(siteRoot, "Source", "components", "PlatformHeader.astro"), "utf8");
assert(platformHeader.includes('sessionKnown={false}'),
    "Public page headers must treat prerendered account state as provisional and revalidate it.");
const documentationHeader = await readFile(path.join(siteRoot, "Source", "components", "DocsHeader.astro"), "utf8");
assert(documentationHeader.includes("SessionAwareAccountLink") && documentationHeader.includes("SiteTitle"),
    "Documentation must retain unified account state and an explicit product title.");
const platformStyles = await readFile(path.join(siteRoot, "Source", "styles", "platform.css"), "utf8");
assert(!platformStyles.includes("var(--space-") && !platformStyles.includes("var(--radius-md") &&
    !platformStyles.includes("var(--surface-strong"),
    "Platform components must not depend on undefined design tokens.");
assert(platformStyles.includes(".editorial-panel:nth-child(4n + 1)") &&
    platformStyles.includes(".editorial-panel:nth-child(4n + 2)") &&
    platformStyles.includes(".editorial-panel:nth-child(4n + 3)") &&
    platformStyles.includes(".editorial-panel:nth-child(4n)"),
    "Editorial card spans must repeat after the first four cards.");
const documentationStyles = await readFile(path.join(siteRoot, "Source", "styles", "keire.css"), "utf8");
assert(documentationStyles.includes("main .hero > .stack") && documentationStyles.includes("place-items: center") &&
    documentationStyles.includes("grid-template-columns: minmax(0, 1fr)") &&
    documentationStyles.includes("grid-column: 1 / -1") && documentationStyles.includes("justify-self: center") &&
    documentationStyles.includes("justify-content: center"),
    "The documentation hero must replace Starlight's media grid and center its complete content stack inside the visual panel.");
assert(!/letter-spacing:\s*-0\.0[6-9]\d*em/.test(`${platformStyles}\n${documentationStyles}`),
    "Shared display typography must not use visually compressed negative tracking.");
const roadmapPage = await readFile(path.join(siteRoot, "Source", "pages", "roadmap", "index.astro"), "utf8");
const readinessProgress = await readFile(path.join(siteRoot, "Source", "components", "ReadinessProgress.astro"), "utf8");
const roadmapModel = await readFile(path.join(siteRoot, "Source", "lib", "roadmap.ts"), "utf8");
const launchReadiness = await readFile(path.join(siteRoot, "Source", "lib", "launchReadiness.ts"), "utf8");
assert(readinessProgress.includes("<progress value={safeCompleted} max={safeTotal}") &&
    readinessProgress.includes("gates passed"),
    "Internal readiness progress must expose native bounded values and useful accessible text.");
assert(roadmapPage.includes("roadmapHorizons") && roadmapPage.includes("Direction, not a delivery guarantee.") &&
    !roadmapPage.includes("ReadinessProgress") && !roadmapPage.includes("completedLaunchChecks") &&
    !roadmapPage.includes("<progress"),
    "The public roadmap must use editorial horizons without internal readiness meters or numeric completion.");
for (const horizon of ['id: "now"', 'id: "next"', 'id: "later"']) {
    assert(roadmapModel.includes(horizon), `The public roadmap is missing ${horizon}.`);
}
assert(roadmapPage.includes("Windows + Linux x86-64") &&
    !roadmapModel.includes("current Windows technology preview") && !roadmapModel.includes("offline signing"),
    "The roadmap contains stale platform or Marketplace publication labels.");
assert(roadmapPage.includes(`${projectVersion} current`) &&
    roadmapPage.includes(`${activeCatalogVersion} sequence 17 active`) &&
    roadmapModel.includes(`Kéire ${projectVersion} current source`) &&
    roadmapModel.includes("Catalog-verified Windows, DEB, and RPM packages"),
    "The roadmap must identify the current release and its active signed package boundary.");
const marketplaceProgress = launchReadiness.split('id: "marketplace"', 2)[1]?.split('id: "operations"', 1)[0] ?? "";
const operationsProgress = launchReadiness.split('id: "operations"', 2)[1] ?? "";
assert(marketplaceProgress.includes("completed: 10") && operationsProgress.includes("completed: 11") &&
    !launchReadiness.includes("offline-signed") &&
    platformStyles.includes(".readiness-progress progress::-webkit-progress-value") &&
    platformStyles.includes(".readiness-progress progress::-moz-progress-bar"),
    "Staff readiness must retain current evidence and cross-browser native-meter styling.");
assert(platformHeader.includes('href="/roadmap/"') && platformHeader.includes('active === "roadmap"'),
    "Roadmap must be discoverable from primary navigation and expose current-page state.");
assert(platformHeader.includes("Engine &amp; Editor"),
    "Primary navigation must use the current Engine and Editor product label.");
assert(platformHeader.includes('href="/changelog/"') && platformHeader.includes('active === "changelog"') &&
    !platformHeader.includes('href="/community/"'),
    "Changelog must be primary navigation while Community moves to the footer.");
const platformFooter = await readFile(path.join(siteRoot, "Source", "components", "PlatformFooter.astro"), "utf8");
assert(platformFooter.includes('href="/roadmap/"') && platformFooter.includes('href="/changelog/"') &&
    platformFooter.includes('href="/community/"') && platformFooter.includes('href="/policies/"'),
    "Roadmap, changelog, Community, and policies must remain reachable from the global footer.");
assert(platformFooter.includes(`Kéire ${projectVersion}`) &&
    platformFooter.includes("current pre-1.0 source") &&
    platformFooter.includes(releaseStatus.state === "active"
        ? `${projectVersion} packages are active`
        : `${projectVersion} Windows validation is in progress`) &&
    platformFooter.includes(`signed ${activeCatalogVersion} packages remain active`),
    "The global footer must identify the current release and catalog-controlled availability.");
const platformHome = await readFile(path.join(siteRoot, "Source", "pages", "index.astro"), "utf8");
assert(platformHome.includes(`softwareVersion: "${projectVersion}"`) &&
    platformHome.includes(`Kéire ${projectVersion} current source`) &&
    platformHome.includes(`Check Hub ${projectVersion} availability`),
    "Home metadata and calls to action must identify the current release.");
const changelogIndex = await readFile(path.join(siteRoot, "Source", "pages", "changelog", "index.astro"), "utf8");
const changelogDetail = await readFile(path.join(siteRoot, "Source", "pages", "changelog", "[version].astro"), "utf8");
const changelogModel = await readFile(path.join(siteRoot, "Source", "lib", "changelog.ts"), "utf8");
const changelogFeed = await readFile(path.join(siteRoot, "Source", "pages", "changelog", "rss.xml.ts"), "utf8");
assert(changelogModel.includes('CHANGELOG.md?raw') && changelogModel.includes('version === "Unreleased"') &&
    changelogModel.includes("groupChanges(changes)") && changelogModel.includes("currentReleasePublished"),
    "The website changelog must derive its complete release train from the canonical repository changelog.");
assert(changelogIndex.includes("currentRelease.highlights") && changelogIndex.includes("releaseNotes.map") &&
    changelogIndex.includes("currentRelease.published") && changelogDetail.includes("release.published") &&
    changelogDetail.includes("ReleaseChangeList") && changelogDetail.includes("availability-legend") &&
    changelogDetail.includes('id="validation-known-limitations"') &&
    changelogDetail.includes("release.evidence.limitations"),
    "Changelog landing and release-detail pages must expose highlights, archive navigation, and availability evidence.");
assert(changelogFeed.includes('Content-Type": "application/rss+xml; charset=utf-8"') &&
    changelogFeed.includes("releaseNotes.filter((release) => release.published).map"),
    "The changelog RSS endpoint must publish the canonical release archive.");

const publisherPage = await readFile(path.join(siteRoot, "Source", "pages", "publisher", "index.astro"), "utf8");
assert(!publisherPage.includes("offline signing") && publisherPage.includes("queued metadata signing"),
    "Publisher copy must describe the automatic metadata-only publication workflow.");
assert(publisherPage.includes(`name="minimumEngineVersion" value="${projectVersion}"`) &&
    publisherPage.includes(`name="managedApiVersion" value="${projectVersion}"`),
    "Publisher release defaults must target the current source/API version without changing its wire protocol.");
const marketplacePage = await readFile(path.join(siteRoot, "Source", "pages", "marketplace", "index.astro"), "utf8");
const marketplacePolicy = await readFile(
    path.join(siteRoot, "Source", "pages", "policies", "marketplace", "index.astro"), "utf8");
const publisherPolicy = await readFile(
    path.join(siteRoot, "Source", "pages", "policies", "publisher", "index.astro"), "utf8");
assert(marketplacePage.includes(`Products remain free for Kéire ${projectVersion}`) &&
    marketplacePolicy.includes(`Free products in Kéire ${projectVersion}`) &&
    marketplacePolicy.includes(`zero minor-unit price for Kéire ${projectVersion}`) &&
    publisherPolicy.includes(`Native plugins remain prohibited for Kéire ${projectVersion}`),
    "Marketplace and publisher policy copy must identify the current product version without changing deployed services.");
for (const contract of [
    'import { Upload } from "tus-js-client"',
    'import { sha256 } from "@noble/hashes/sha2.js"',
    "hashChunkBytes = 4 * 1024 * 1024",
    "chunkSize: 6 * 1024 * 1024",
    'headers: { "x-signature": reservation.uploadToken }',
    'contentType: "application/vnd.keire.asset-package"',
    "cancelReservation",
    "data-submit-version",
    "/publisher/v1/versions/",
]) {
    assert(publisherPage.includes(contract), `Publisher package upload is missing ${contract}.`);
}
const caddyConfig = await readFile(path.join(repositoryRoot, "Services", "KeireDistributionService", "Deployment",
    "Caddyfile.example"), "utf8");
assert(caddyConfig.includes("connect-src 'self' https://khjduyjamzwumhducmou.supabase.co " +
    "https://khjduyjamzwumhducmou.storage.supabase.co"),
    "Publisher uploads must allow the exact Supabase API and direct-Storage origins through the website CSP.");
for (const contract of ["health_method HEAD", "health_fails 3", "health_passes 2"]) {
    assert(caddyConfig.includes(contract), `Caddy website liveness is missing ${contract}.`);
}
const publisherUploadRoutes = (await Promise.all([
    "Source/pages/publisher/v1/uploads/index.ts",
    "Source/pages/publisher/v1/uploads/[id]/index.ts",
    "Source/pages/publisher/v1/uploads/[id]/complete.ts",
].map((relativePath) => readFile(path.join(siteRoot, ...relativePath.split("/")), "utf8")))).join("\n");
assert(publisherUploadRoutes.includes("requireAal2") && publisherUploadRoutes.includes("publisher_portal_enabled"),
    "Publisher upload adapters must require MFA and the private authoring feature flag.");
assert(publisherUploadRoutes.includes('functions.invoke("marketplace-publisher"'),
    "Publisher upload adapters must use the hardened Edge transition boundary.");
assert(publisherUploadRoutes.includes('typeof data.productId !== "string"'),
    "Publisher upload adapters must return the product identity created by the reservation.");
assert(publisherUploadRoutes.includes("export const GET") &&
    publisherUploadRoutes.includes('from("marketplace_validation_reports")'),
    "Publisher uploads must expose an authenticated, RLS-scoped validation status endpoint.");
assert(publisherPage.includes("data-validation-upload") && publisherPage.includes("startValidationPolling") &&
    publisherPage.includes('cache: "no-store"') && publisherPage.includes("terminalUploadStates"),
    "Publisher validation activity must refresh asynchronous results without leaving stale pending labels.");
assert(!publisherPage.includes("publisherReleaseGates") && !publisherPage.includes("completedPublisherGates") &&
    !publisherPage.includes("<ReadinessProgress") && !publisherPage.includes('href="#overview"') &&
    publisherPage.includes('href="#new-release" aria-current="page"'),
    "Publisher authoring must begin with actionable release work instead of a decorative readiness overview.");
assert(publisherPage.includes("readyForStaff") && publisherPage.includes('id="ready-for-review"') &&
    publisherPage.includes("Upload and Staff submission are separate safeguards.") &&
    publisherPage.includes("Submit to Staff review"),
    "Validated packages must expose an explicit, prominent publisher-to-Staff submission checkpoint.");
const publisherSubmitRoute = await readFile(path.join(siteRoot, "Source", "pages", "publisher", "v1", "versions",
    "[id]", "submit.ts"), "utf8");
assert(publisherSubmitRoute.includes("requireAal2") &&
    publisherSubmitRoute.includes('functions.invoke("marketplace-publisher"'),
    "Validated package submission must use the MFA-protected publisher transition boundary.");
const staffPage = await readFile(path.join(siteRoot, "Source", "pages", "admin", "marketplace", "index.astro"), "utf8");
for (const contract of [
    'from("platform_staff_members")',
    "application.decide",
    "submission.decide",
    "report.decide",
    "staff.set",
    "feature.set",
    "data-moderation-form",
]) {
    assert(staffPage.includes(contract), `Staff operations center is missing ${contract}.`);
}
assert(staffPage.includes("launchReadinessTracks") && staffPage.includes('id="readiness"') &&
    staffPage.includes('href="/docs/reference/production-readiness-review/"') &&
    !staffPage.includes('href="/roadmap/"'),
    "Staff operations must retain detailed launch evidence independently of the public roadmap.");
assert(staffPage.includes(readinessAuditLabel) &&
    staffPage.includes("Marketplace launch checklist · current source"),
    "Staff readiness labels are stale.");
assert(staffPage.includes('activeSubmissions') && staffPage.includes('signingSubmissions') &&
    staffPage.includes('submissionHistory') && staffPage.includes('id="review-history"'),
    "Terminal package decisions must leave the active staff queue while remaining available as review history.");
assert(staffPage.includes('id="official-releases"') &&
    staffPage.includes('/publisher/?product=${encodeURIComponent(product.id)}#new-release') &&
    publisherPage.includes('requestedProductId') &&
    publisherPage.includes('name="productId" value={requestedProduct?.id ?? ""}') &&
    publisherPage.includes("Upload new version"),
    "Official catalog drafts must enter the standard publisher release pipeline through a prefilled product identity.");
for (const contract of [
    'name="publisherId"',
    'name="productName"',
    'name="categoryId"',
    'name="license"',
    'name="productSummary"',
    "publisherId: values.get(\"publisherId\")",
    "categoryId: values.get(\"categoryId\")",
    "productName: values.get(\"productName\")",
    "productSummary: values.get(\"productSummary\")",
]) {
    assert(publisherPage.includes(contract), `Named publisher product upload is missing ${contract}.`);
}
assert(publisherPage.includes('isNeonForgeUploadSample') &&
    publisherPage.includes('"1.0.0"') &&
    publisherPage.includes('"pbr, shader-graph, material-graph, vfx-graph"'),
    "The mixed-content upload sample must prefill its exact version and renderer capabilities.");
assert(staffPage.includes("Withdraw approval") && staffPage.includes("staff-withdrawal-form"),
    "An administrator must be able to withdraw pre-publication signing approval without deleting audit evidence.");
assert(!staffPage.includes("app_metadata"),
    "Staff page authorization must use current database roles instead of browser JWT metadata.");
const platformLayout = await readFile(path.join(siteRoot, "Source", "layouts", "PlatformLayout.astro"), "utf8");
for (const metadata of ["og:site_name", "twitter:title", "twitter:description", "twitter:image"]) {
    assert(platformLayout.includes(metadata), `Platform metadata is missing ${metadata}.`);
}
const staffAwareHeader = await readFile(path.join(siteRoot, "Source", "components", "PlatformHeader.astro"), "utf8");
assert(platformLayout.includes('from("platform_staff_members")') &&
    staffAwareHeader.includes('href="/admin/marketplace/"'),
    "Active staff must receive a discoverable, database-authorized operations link.");
const staffActionRoute = await readFile(path.join(siteRoot, "Source", "pages", "admin", "marketplace", "v1",
    "actions", "index.ts"), "utf8");
assert(staffActionRoute.includes("requireAal2") &&
    staffActionRoute.includes('functions.invoke("marketplace-moderation"'),
    "Staff actions must use the MFA-protected moderation transition boundary.");
const staffPublicationRoute = await readFile(path.join(siteRoot, "Source", "pages", "admin", "marketplace", "v1",
    "publications", "index.ts"), "utf8");
assert(!staffPage.includes("data-publication-form") && staffPage.includes("Automatic publication queue") &&
    staffPage.includes("Approve and queue publication"),
    "Approved packages must enter the automatic metadata-only publication queue without a browser-held envelope.");
assert(!staffPage.includes('/admin/marketplace/v1/publications/') &&
    staffPublicationRoute.includes("requireAal2") &&
    staffPublicationRoute.includes('functions.invoke("marketplace-publication"'),
    "The legacy offline recovery route must remain an administrator-only publication boundary, not a staff UI path.");
for (const relativePath of [
    "Source/pages/contact/index.astro",
    "Source/pages/account/create/index.astro",
    "Source/pages/account/recover/index.astro",
    "Source/pages/account/sign-in.astro",
    "Source/pages/account/update-password/index.astro",
]) {
    const page = await readFile(path.join(siteRoot, ...relativePath.split("/")), "utf8");
    assert((page.match(/<h1\b/g) ?? []).length === 1,
        `Website page must expose exactly one primary heading: ${relativePath}.`);
}
const marketplaceApi = await readFile(path.join(siteRoot, "Source", "lib", "api.ts"), "utf8");
const marketplaceCatalogRoute = await readFile(path.join(siteRoot, "Source", "pages", "marketplace", "v1", "catalog",
    "index.ts"), "utf8");
const marketplaceLibraryRoute = await readFile(path.join(siteRoot, "Source", "pages", "marketplace", "v1", "library",
    "index.ts"), "utf8");
assert(marketplaceApi.includes("decodeCatalogCursor") && marketplaceApi.includes("decodeLibraryCursor") &&
    marketplaceApi.includes('kind: "catalog"') && marketplaceApi.includes('kind: "library"'),
    "Marketplace cursors must be typed so catalog and library positions cannot be interchanged.");
assert(!marketplaceCatalogRoute.includes(".range(") && !marketplaceLibraryRoute.includes(".range(") &&
    marketplaceCatalogRoute.includes("published_at.lt") && marketplaceCatalogRoute.includes("id.gt") &&
    marketplaceLibraryRoute.includes("granted_at.lt") && marketplaceLibraryRoute.includes("id.gt"),
    "Marketplace catalog and library pagination must use stable keysets instead of deep database offsets.");
const marketplaceProductPage = await readFile(path.join(siteRoot, "Source", "pages", "marketplace", "[publisher]",
    "[product].astro"), "utf8");
const marketplaceClaimRoute = await readFile(path.join(siteRoot, "Source", "pages", "marketplace", "v1", "claims",
    "index.ts"), "utf8");
for (const contract of [
    "claimAttemptKey",
    "AbortController",
    'cache: "no-store"',
    'credentials: "same-origin"',
    'button.removeAttribute("aria-busy")',
    "Check My Assets; if it is not there, retry safely.",
    'href="/account/#library"',
]) {
    assert(marketplaceProductPage.includes(contract), `Marketplace claim recovery is missing ${contract}.`);
}
assert(marketplaceProductPage.includes('from("marketplace_entitlements")') &&
    marketplaceProductPage.includes('button.textContent = "In My Assets"'),
    "Marketplace products must render and retain their personal-library ownership state.");
assert(marketplaceClaimRoute.includes("timeout: 12_000"),
    "Marketplace claims must bound the Edge Function request before the browser timeout.");
for (const style of [".staff-console", ".staff-metrics", ".staff-review-card", ".staff-detail-grid",
    ".staff-subsection-heading", ".staff-withdrawal-form"]) {
    assert(platformStyles.includes(style), `Staff operations styling is missing ${style}.`);
}

console.log(`Documentation source validation passed for ${allDocSources.length} canonical guides, ${docGroups.length} navigation groups, ${mermaidCount} Mermaid diagrams, local links, and ${versionContracts.length} schema contracts.`);
