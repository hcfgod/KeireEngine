import { createHash } from "node:crypto";
import { mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const siteRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const outputRoot = path.join(siteRoot, "dist", "client");
const assetRoot = path.join(outputRoot, "_astro");

async function collectHtml(directory) {
    const results = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const absolute = path.join(directory, entry.name);
        if (entry.isDirectory()) {
            results.push(...await collectHtml(absolute));
        } else if (entry.isFile() && entry.name.endsWith(".html")) {
            results.push(absolute);
        }
    }
    return results;
}

function assetName(kind, content) {
    const digest = createHash("sha256").update(content).digest("hex").slice(0, 16);
    return `keire-csp-${digest}.${kind}`;
}

await mkdir(assetRoot, { recursive: true });
const written = new Set();
const extractedAttributeStyles = new Map();
let extractedStyles = 0;
let extractedScripts = 0;
const documents = [];

for (const htmlPath of await collectHtml(outputRoot)) {
    let html = await readFile(htmlPath, "utf8");
    const pending = [];
    html = html.replace(/<style(?:\s[^>]*)?>([\s\S]*?)<\/style>/gi, (_match, content) => {
        if (!content.trim()) {
            return "";
        }
        const name = assetName("css", content);
        pending.push({ name, content });
        ++extractedStyles;
        return `<link rel="stylesheet" href="/docs/_astro/${name}">`;
    });
    html = html.replace(/<script(?![^>]*\bsrc=)([^>]*)>([\s\S]*?)<\/script>/gi, (match, attributes, content) => {
        if (!content.trim()) {
            return "";
        }
        const type = /\btype=["']([^"']+)["']/i.exec(attributes)?.[1] ?? "text/javascript";
        if (type === "application/ld+json") {
            return match;
        }
        if (!new Set(["text/javascript", "application/javascript", "module"]).has(type)) {
            throw new Error(`Unsupported inline script type '${type}' in ${htmlPath}.`);
        }
        const name = assetName("js", content);
        pending.push({ name, content });
        ++extractedScripts;
        return `<script${attributes} src="/_astro/${name}"></script>`;
    });
    html = html.replace(/\sstyle=(?:"([^"]*)"|'([^']*)')/gi, (_match, doubleQuoted, singleQuoted) => {
        const declarations = (doubleQuoted ?? singleQuoted)
            .replaceAll("&quot;", "\"")
            .replaceAll("&#39;", "'")
            .replaceAll("&amp;", "&")
            .replaceAll("&lt;", "<")
            .replaceAll("&gt;", ">");
        const digest = createHash("sha256").update(declarations).digest("hex").slice(0, 12);
        extractedAttributeStyles.set(digest, declarations);
        return ` data-keire-style-${digest}=""`;
    });
    for (const asset of pending) {
        if (!written.has(asset.name)) {
            await writeFile(path.join(assetRoot, asset.name), asset.content, "utf8");
            written.add(asset.name);
        }
    }
    documents.push({ htmlPath, html });
}

if (extractedAttributeStyles.size > 0) {
    const css = [...extractedAttributeStyles]
        .sort(([left], [right]) => left.localeCompare(right))
        .map(([digest, declarations]) => `[data-keire-style-${digest}] { ${declarations} }`)
        .join("\n");
    const name = assetName("css", css);
    await writeFile(path.join(assetRoot, name), `${css}\n`, "utf8");
    written.add(name);
    for (const document of documents) {
        document.html = document.html.replace("</head>", `<link rel="stylesheet" href="/_astro/${name}"></head>`);
    }
}

for (const { htmlPath, html } of documents) {
    await writeFile(htmlPath, html, "utf8");
}

console.log(`Externalized ${extractedStyles} inline styles, ${extractedScripts} inline scripts, and ${extractedAttributeStyles.size} inline style values into ${written.size} self-hosted CSP assets.`);
