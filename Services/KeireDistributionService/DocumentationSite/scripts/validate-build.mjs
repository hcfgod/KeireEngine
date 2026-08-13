import { access, readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { allDocSources, sourcePathToSlug } from "../doc-library.mjs";

const siteRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const outputRoot = path.join(siteRoot, "dist", "client");

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

async function collectFiles(directory) {
    const results = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const absolute = path.join(directory, entry.name);
        if (entry.isDirectory()) {
            results.push(...await collectFiles(absolute));
        } else if (entry.isFile()) {
            results.push(absolute);
        }
    }
    return results;
}

await access(path.join(outputRoot, "index.html"));
await access(path.join(outputRoot, "changelog", "index.html"));
for (const version of ["0.3.1", "0.3.0", "0.2.0", "0.1.0"]) {
    await access(path.join(outputRoot, "changelog", version, "index.html"));
}
const changelogFeed = await readFile(path.join(outputRoot, "changelog", "rss.xml"), "utf8");
assert(changelogFeed.startsWith('<?xml version="1.0" encoding="UTF-8"?>') &&
    changelogFeed.includes("<title>Kéire Engine changelog</title>") &&
    changelogFeed.includes("/changelog/0.3.1/"),
    "Built changelog feed is missing its canonical metadata or current release.");
for (const sourcePath of allDocSources) {
    await access(path.join(outputRoot, "docs", ...sourcePathToSlug(sourcePath).split("/"), "index.html"));
}

const files = await collectFiles(outputRoot);
const outputFiles = new Set(files.map((file) => path.resolve(file)));
const htmlFiles = files.filter((file) => file.endsWith(".html"));
const serverRoutePrefixes = ["account/", "health/", "marketplace/", "publisher/"];
let mermaidDiagramCount = 0;
assert(htmlFiles.length >= allDocSources.length + 2, "Documentation output is missing a landing page, guide page, or branded 404 page.");

for (const htmlPath of htmlFiles) {
    const html = await readFile(htmlPath, "utf8");
    assert(/<html\b[^>]*\blang=/i.test(html), `Missing document language in ${htmlPath}.`);
    assert(/<title>[^<]+<\/title>/i.test(html), `Missing page title in ${htmlPath}.`);
    assert(/<meta\s+name="description"\s+content="[^"]+/i.test(html), `Missing page description in ${htmlPath}.`);
    assert(/<main\b/i.test(html), `Missing main landmark in ${htmlPath}.`);
    assert(/<h1\b/i.test(html), `Missing level-one heading in ${htmlPath}.`);
    assert(!/<style(?:\s|>)/i.test(html), `Inline style block violates the production CSP in ${htmlPath}.`);
    for (const script of html.matchAll(/<script(?![^>]*\bsrc=)([^>]*)>([\s\S]*?)<\/script>/gi)) {
        const type = /\btype=["']([^"']+)["']/i.exec(script[1])?.[1] ?? "text/javascript";
        assert(type === "application/ld+json" || !script[2].trim(), `Inline script violates the production CSP in ${htmlPath}.`);
        if (type === "application/ld+json" && script[2].trim()) {
            JSON.parse(script[2]);
        }
    }
    assert(!/\son[a-z]+\s*=/i.test(html), `Inline event handler violates the production CSP in ${htmlPath}.`);
    assert(!/\sstyle\s*=/i.test(html), `Inline style attribute violates the production CSP in ${htmlPath}.`);
    assert(!/<script\b[^>]*\bsrc=["']https?:\/\//i.test(html), `External script resource found in ${htmlPath}.`);
    assert(!/<link\b(?=[^>]*\brel=["'](?:stylesheet|preload|modulepreload)["'])[^>]*\bhref=["']https?:\/\//i.test(html), `External style or preload resource found in ${htmlPath}.`);
    assert(!/<pre\b[^>]*\bdata-language=["']mermaid["']/i.test(html), `Unrendered Mermaid code block found in ${htmlPath}.`);
    const diagrams = [...html.matchAll(/<figure\b[^>]*\bclass=["'][^"']*\bkeire-mermaid\b[^"']*["']/gi)];
    mermaidDiagramCount += diagrams.length;
    if (diagrams.length > 0) {
        assert(/<svg\b[^>]*\bclass=["']keire-mermaid__svg["'][^>]*\brole=["']img["']/i.test(html), `Mermaid SVG lacks its accessible image role in ${htmlPath}.`);
        assert(!/<svg\b[^>]*>(?:(?!<\/svg>)[\s\S])*?<link\b[^>]*\brel=["']stylesheet["']/i.test(html), `Mermaid SVG contains an invalid embedded stylesheet link in ${htmlPath}.`);
    }
    for (const match of html.matchAll(/\b(?:href|src)=["']([^"']+)["']/gi)) {
        const value = match[1];
        if (!value.startsWith("/")) {
            continue;
        }
        const url = new URL(value, "https://keireengine.duckdns.org");
        let relative = decodeURIComponent(url.pathname.slice(1));
        if (!relative || relative.endsWith("/")) {
            relative = `${relative}index.html`;
        }
        const target = path.resolve(outputRoot, ...relative.split("/"));
        assert(target.startsWith(`${path.resolve(outputRoot)}${path.sep}`), `Documentation link escapes the output root in ${htmlPath}: ${value}`);
        if (!outputFiles.has(target) && serverRoutePrefixes.some((prefix) => relative.startsWith(prefix))) {
            continue;
        }
        assert(outputFiles.has(target), `Broken documentation link in ${htmlPath}: ${value}`);
    }
}
assert(mermaidDiagramCount > 0, "Documentation output contains no rendered Mermaid diagrams.");

for (const assetPath of files.filter((file) => /\.(?:css|js|html|svg)$/i.test(file))) {
    const content = await readFile(assetPath, "utf8");
    assert(!/fonts\.googleapis\.com|fonts\.gstatic\.com/i.test(content), `External font reference found in ${assetPath}.`);
}

const inventory = JSON.parse(await readFile(path.join(outputRoot, "doc-inventory.json"), "utf8"));
assert(inventory.schemaVersion === 1 && inventory.documents?.length === allDocSources.length, "Built documentation inventory is incomplete.");
assert(new Set(inventory.documents.map(({ source }) => source)).size === allDocSources.length, "Built documentation inventory contains duplicate sources.");

assert(files.some((file) => /[\\/]pagefind[\\/]pagefind\.js$/.test(file)), "Local Pagefind search output is missing.");
assert(files.some((file) => /[\\/]sitemap-(?:index|0)\.xml$/.test(file)), "Platform sitemap output is missing.");

console.log(`Documentation build validation passed for ${htmlFiles.length} pages, ${allDocSources.length} canonical guides, and ${mermaidDiagramCount} rendered diagrams.`);
