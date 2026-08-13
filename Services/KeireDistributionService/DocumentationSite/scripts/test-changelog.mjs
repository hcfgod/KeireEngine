import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { parseChangelog } from "../Source/lib/changelog-model.mjs";

const siteRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repositoryRoot = path.resolve(siteRoot, "..", "..", "..");

const fixture = [
    "# Changelog",
    "",
    "## Unreleased",
    "",
    "- A source change with",
    "  a multiline description.",
    "",
    "## 0.3.1 - 2026-08-12",
    "",
    "### Added",
    "",
    "- A released capability.",
    "",
    "### Fixed",
    "",
    "- A resolved defect.",
].join("\n");

const releases = parseChangelog(fixture);
assert.equal(releases.length, 2);
assert.deepEqual(releases.map(({ version }) => version), ["Unreleased", "0.3.1"]);
assert.equal(releases[0].groups[0].label, "Highlights");
assert.equal(releases[0].groups[0].entries[0], "A source change with a multiline description.");
assert.equal(releases[1].date, "2026-08-12");
assert.deepEqual(releases[1].groups.map(({ label }) => label), ["Added", "Fixed"]);
assert.throws(() => parseChangelog(/** @type {any} */ (null)), /must be a string/);
assert.throws(() => parseChangelog("## 0.3.1\n- One\n## 0.3.1\n- Two"), /Duplicate changelog release/);

const canonicalSource = await readFile(path.join(repositoryRoot, "CHANGELOG.md"), "utf8");
const canonicalReleases = parseChangelog(canonicalSource);
const current = canonicalReleases.find(({ version }) => version === "0.3.1");
const sourceUpdates = canonicalReleases.find(({ version }) => version === "Unreleased");
assert(current && current.groups.flatMap(({ entries }) => entries).length >= 29,
    "The canonical 0.3.1 release record is unexpectedly incomplete.");
assert(sourceUpdates && sourceUpdates.groups.flatMap(({ entries }) => entries).length > 0,
    "Current source updates must remain visible alongside the 0.3.1 release train.");

console.log(`Changelog parsing passed for ${canonicalReleases.length} releases and ${current.groups.flatMap(({ entries }) => entries).length} canonical 0.3.1 entries.`);
