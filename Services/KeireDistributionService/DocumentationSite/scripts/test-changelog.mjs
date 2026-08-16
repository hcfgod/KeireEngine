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
const projectConfiguration = await readFile(path.join(repositoryRoot, "Config", "Project.conf"), "utf8");
const currentVersion = /^PROJECT_VERSION=(\S+)$/m.exec(projectConfiguration)?.[1];
const current = canonicalReleases.find(({ version }) => version === currentVersion);
assert(currentVersion && current && current.groups.flatMap(({ entries }) => entries).length >= 10,
    `The canonical ${currentVersion} release record is unexpectedly incomplete.`);
assert(/^## Unreleased$/m.test(canonicalSource), "The canonical changelog must retain an Unreleased section.");

console.log(`Changelog parsing passed for ${canonicalReleases.length} releases and ${current.groups.flatMap(({ entries }) => entries).length} canonical ${currentVersion} entries.`);
