import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const siteRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const pagefindRoot = path.join(siteRoot, "dist", "client", "pagefind");
const moduleUrl = pathToFileURL(path.join(pagefindRoot, "pagefind.js"));
const remoteBaseUrl = process.env.KEIRE_DOCS_BASE_URL?.replace(/\/$/, "");
const pagefindBaseUrl = remoteBaseUrl
    ? `${remoteBaseUrl}/pagefind/`
    : pathToFileURL(`${pagefindRoot}${path.sep}`).href;
const nativeFetch = globalThis.fetch;

globalThis.fetch = async (input, init) => {
    const url = new URL(input instanceof Request ? input.url : String(input));
    if (url.protocol !== "file:") {
        return nativeFetch(input, init);
    }
    try {
        const bytes = await readFile(fileURLToPath(url));
        const type = url.pathname.endsWith(".wasm") ? "application/wasm" : "application/octet-stream";
        return new Response(bytes, { status: 200, headers: { "content-type": type } });
    } catch (error) {
        if (error?.code === "ENOENT") {
            return new Response(null, { status: 404 });
        }
        throw error;
    }
};

try {
    const pagefind = await import(`${moduleUrl.href}?validation=1`);
    const search = pagefind.createInstance({
        basePath: pagefindBaseUrl,
        baseUrl: "/",
        language: "en",
        noWorker: true,
    });
    await search.init();
    const response = await search.search("deterministic runtime");
    if (!response || response.results.length === 0) {
        throw new Error("Pagefind returned no results for a known documentation query.");
    }
    const result = await response.results[0].data();
    if (!result?.meta?.title || !String(result.url).startsWith("/docs/reference/")) {
        throw new Error("Pagefind returned a malformed native documentation result.");
    }
    await search.destroy();
    console.log(`Pagefind search validation passed with ${response.results.length} result(s); first result: ${result.meta.title}.`);
} finally {
    globalThis.fetch = nativeFetch;
}
