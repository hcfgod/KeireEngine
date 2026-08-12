import { access, copyFile, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { renderMermaidSVG } from "beautiful-mermaid";

import { allDocSources, docGroups, sourcePathToSlug } from "../doc-library.mjs";

const siteRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repositoryRoot = path.resolve(siteRoot, "..", "..", "..");
const sourceRoot = path.join(repositoryRoot, "Docs");
const destinationRoot = path.join(siteRoot, "Source", "content", "docs", "reference");
const inventoryPath = path.join(siteRoot, "public", "doc-inventory.json");
const publicAssetRoot = path.join(siteRoot, "public", "assets");
const repositoryUrl = "https://github.com/hcfgod/KeireEngine";
const mermaidTheme = {
    bg: "var(--sl-color-black)",
    fg: "var(--sl-color-gray-1)",
    line: "var(--sl-color-accent)",
    accent: "var(--sl-color-accent)",
    muted: "var(--sl-color-gray-2)",
    surface: "var(--sl-color-gray-6)",
    border: "var(--sl-color-gray-4)",
    font: "Inter",
    transparent: true,
};

function assertGeneratedDestination() {
    const relative = path.relative(siteRoot, destinationRoot);
    if (relative.startsWith("..") || path.isAbsolute(relative) || !relative.replaceAll("\\", "/").endsWith("Source/content/docs/reference")) {
        throw new Error(`Refusing to replace unexpected documentation destination: ${destinationRoot}`);
    }
}

async function collectMarkdown(root, directory = root) {
    const paths = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const absolute = path.join(directory, entry.name);
        if (entry.isDirectory()) {
            paths.push(...await collectMarkdown(root, absolute));
        } else if (entry.isFile() && entry.name.toLowerCase().endsWith(".md")) {
            paths.push(path.relative(root, absolute).replaceAll("\\", "/"));
        }
    }
    return paths.sort((left, right) => left.localeCompare(right));
}

function stripMarkdown(value) {
    return value
        .replace(/<!--.*?-->/g, "")
        .replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1")
        .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
        .replace(/[`*_~]/g, "")
        .replace(/<[^>]+>/g, "")
        .replace(/\s+/g, " ")
        .trim();
}

function extractDocument(source, sourcePath) {
    const normalized = source.replace(/^\uFEFF/, "").replaceAll("\r\n", "\n");
    const lines = normalized.split("\n");
    const headingIndex = lines.findIndex((line) => /^#\s+\S/.test(line));
    if (headingIndex < 0) {
        throw new Error(`Documentation source has no level-one heading: Docs/${sourcePath}`);
    }

    const title = stripMarkdown(lines[headingIndex].replace(/^#\s+/, ""));
    lines.splice(headingIndex, 1);

    let description = "";
    for (let index = headingIndex; index < lines.length; ++index) {
        const line = lines[index].trim();
        if (!line || line.startsWith("<!--") || line.startsWith("#") || line.startsWith("|") || line.startsWith("-") || line.startsWith("*") || line.startsWith("```") || /^\d+\.\s/.test(line)) {
            continue;
        }
        const paragraph = [line];
        for (++index; index < lines.length && lines[index].trim(); ++index) {
            paragraph.push(lines[index].trim());
        }
        description = stripMarkdown(paragraph.join(" "));
        if (description) {
            break;
        }
    }
    if (!description) {
        description = `${title} documentation for Kéire Engine.`;
    }
    if (description.length > 220) {
        description = `${description.slice(0, 217).trimEnd()}…`;
    }
    return { title, description, body: lines.join("\n").replace(/^\n+/, "") };
}

const sourceToSlug = new Map(allDocSources.map((sourcePath) => [sourcePath, `docs/${sourcePathToSlug(sourcePath)}`]));

function rewriteTarget(target, sourcePath) {
    const trimmed = target.trim();
    if (!trimmed || trimmed.startsWith("#") || /^[a-z][a-z0-9+.-]*:/i.test(trimmed) || trimmed.startsWith("//")) {
        return target;
    }

    const match = /^(.*?)(#[^\s]*)?(\s+['"].*['"])?$/.exec(trimmed);
    if (!match) {
        return target;
    }
    const [, rawPath, fragment = "", title = ""] = match;
    const decodedPath = decodeURIComponent(rawPath.replace(/^<|>$/g, ""));
    const sourceDirectory = path.posix.dirname(sourcePath);
    const resolvedFromDocs = path.posix.normalize(path.posix.join(sourceDirectory, decodedPath));

    if (decodedPath.toLowerCase().endsWith(".md") && sourceToSlug.has(resolvedFromDocs)) {
        return `/${sourceToSlug.get(resolvedFromDocs)}/${fragment}${title}`;
    }

    const repositoryRelative = path.posix.normalize(path.posix.join("Docs", sourceDirectory, decodedPath));
    if (!repositoryRelative.startsWith("../") && repositoryRelative !== "..") {
        const absolute = path.join(repositoryRoot, ...repositoryRelative.split("/"));
        return `${repositoryUrl}/blob/master/${repositoryRelative}${fragment}${title}`;
    }

    const rootRelative = repositoryRelative.replace(/^\.\.\//, "");
    return `${repositoryUrl}/blob/master/${rootRelative}${fragment}${title}`;
}

function rewriteLinks(body, sourcePath) {
    const lines = body.split("\n");
    let fence = null;
    return lines.map((line) => {
        const fenceMatch = /^\s*(```+|~~~+)/.exec(line);
        if (fenceMatch) {
            const marker = fenceMatch[1][0];
            fence = fence === marker ? null : (fence ?? marker);
            return line;
        }
        if (fence) {
            return line;
        }
        return line.replace(/\]\(([^)]+)\)/g, (_match, target) => `](${rewriteTarget(target, sourcePath)})`);
    }).join("\n");
}

function escapeHtml(value) {
    return value
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;");
}

function renderMermaidDiagrams(body, title, sourcePath) {
    let diagramIndex = 0;
    const documentId = sourcePath
        .replace(/\.md$/i, "")
        .replace(/[^a-z0-9]+/gi, "-")
        .replace(/^-|-$/g, "")
        .toLowerCase();

    const rendered = body.replace(/^```mermaid[\t ]*\n([\s\S]*?)\n```[\t ]*$/gm, (_match, source) => {
        ++diagramIndex;
        const diagramId = `diagram-${documentId}-${diagramIndex}`;
        let svg;
        try {
            svg = renderMermaidSVG(source, mermaidTheme);
        } catch (error) {
            throw new Error(`Unable to render Mermaid diagram ${diagramIndex} in Docs/${sourcePath}: ${error.message}`, { cause: error });
        }

        // The shared site stylesheet owns the renderer's small SVG rule set. Dropping the embedded stylesheet keeps
        // the diagram valid under the strict CSP and prevents the finalizer from inserting an HTML link inside SVG.
        svg = svg
            .replace(/<style(?:\s[^>]*)?>[\s\S]*?<\/style>/gi, "")
            .replace(
                "<svg ",
                `<svg class="keire-mermaid__svg" role="img" aria-labelledby="${diagramId}" preserveAspectRatio="xMidYMid meet" `,
            );
        const securityProbe = svg.replace('xmlns="http://www.w3.org/2000/svg"', "");
        if (!svg.startsWith("<svg") || /<script\b|\bon[a-z]+\s*=|https?:\/\//i.test(securityProbe)) {
            throw new Error(`Mermaid diagram ${diagramIndex} in Docs/${sourcePath} produced unsafe or external SVG content.`);
        }

        return [
            `<figure class="keire-mermaid not-content">`,
            `  <figcaption id="${diagramId}" class="keire-mermaid__caption"><span>${escapeHtml(title)} diagram</span><span class="keire-mermaid__pan-hint">Swipe or scroll to explore</span></figcaption>`,
            `  <div class="keire-mermaid__viewport">${svg}</div>`,
            `  <details class="keire-mermaid__source">`,
            `    <summary>View Mermaid source</summary>`,
            `    <pre><code>${escapeHtml(source)}</code></pre>`,
            `  </details>`,
            `</figure>`,
        ].join("\n");
    });

    if (/^```mermaid\b/m.test(rendered)) {
        throw new Error(`Unrendered Mermaid fence remains in Docs/${sourcePath}.`);
    }
    return { body: rendered, diagramCount: diagramIndex };
}

function destinationFor(sourcePath) {
    return path.join(siteRoot, "Source", "content", "docs", `${sourcePathToSlug(sourcePath)}.md`);
}

async function main() {
    assertGeneratedDestination();
    const actualSources = await collectMarkdown(sourceRoot);
    const expectedSources = [...allDocSources].sort((left, right) => left.localeCompare(right));
    if (JSON.stringify(actualSources) !== JSON.stringify(expectedSources)) {
        const actual = new Set(actualSources);
        const expected = new Set(expectedSources);
        const missing = expectedSources.filter((sourcePath) => !actual.has(sourcePath));
        const unexpected = actualSources.filter((sourcePath) => !expected.has(sourcePath));
        throw new Error(`Documentation inventory is stale. Missing: ${missing.join(", ") || "none"}; unexpected: ${unexpected.join(", ") || "none"}.`);
    }

    await rm(destinationRoot, { recursive: true, force: true });
    await mkdir(destinationRoot, { recursive: true });
    await mkdir(path.dirname(inventoryPath), { recursive: true });
    await mkdir(publicAssetRoot, { recursive: true });
    for (const assetName of ["inter-variable.ttf", "keire.png", "hero-cinematic.png"]) {
        await copyFile(
            path.join(siteRoot, "..", "Website", "assets", assetName),
            path.join(publicAssetRoot, assetName),
        );
    }
    for (const assetName of ["downloads.js", "preview-downloads.json"]) {
        await copyFile(
            path.join(siteRoot, "..", "Website", "assets", assetName),
            path.join(publicAssetRoot, assetName),
        );
    }
    await copyFile(
        path.join(siteRoot, "..", "Website", "site.webmanifest"),
        path.join(siteRoot, "public", "site.webmanifest"),
    );

    const inventory = [];
    let diagramCount = 0;
    for (const [groupIndex, group] of docGroups.entries()) {
        for (const [documentIndex, sourcePath] of group.files.entries()) {
            const sourceFile = path.join(sourceRoot, ...sourcePath.split("/"));
            await access(sourceFile);
            const source = await readFile(sourceFile, "utf8");
            const { title, description, body } = extractDocument(source, sourcePath);
            const rendered = renderMermaidDiagrams(body, title, sourcePath);
            diagramCount += rendered.diagramCount;
            const slug = `docs/${sourcePathToSlug(sourcePath)}`;
            const destination = destinationFor(sourcePath);
            await mkdir(path.dirname(destination), { recursive: true });
            const frontmatter = [
                "---",
                `title: ${JSON.stringify(title)}`,
                `description: ${JSON.stringify(description)}`,
                `slug: ${slug}`,
                `editUrl: ${repositoryUrl}/edit/master/Docs/${sourcePath}`,
                "sidebar:",
                `  order: ${(groupIndex + 1) * 100 + documentIndex}`,
                "---",
                "",
            ].join("\n");
            await writeFile(destination, `${frontmatter}${rewriteLinks(rendered.body, sourcePath).trimEnd()}\n`, "utf8");
            inventory.push({ source: `Docs/${sourcePath}`, slug, title, group: group.label });
        }
    }

    await writeFile(inventoryPath, `${JSON.stringify({ schemaVersion: 1, documents: inventory }, null, 2)}\n`, "utf8");
    console.log(`Prepared ${inventory.length} canonical Kéire documentation pages and ${diagramCount} build-time diagrams.`);
}

await main();
