import path from "node:path";
import { pathToFileURL } from "node:url";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const sourceTest = path.join(
    root,
    "Services",
    "KeireDistributionService",
    "DocumentationSite",
    "scripts",
    "test-source.mjs",
);

await import(pathToFileURL(sourceTest));
