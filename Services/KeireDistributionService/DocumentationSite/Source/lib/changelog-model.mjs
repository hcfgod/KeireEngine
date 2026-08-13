/**
 * @typedef {{ label: string, entries: string[] }} ParsedChangelogGroup
 * @typedef {{ version: string, date: string | null, groups: ParsedChangelogGroup[] }} ParsedChangelogRelease
 */

function normalizeEntry(value) {
    return value.replace(/\s+/g, " ").trim();
}

/**
 * Parse the intentionally small Keep a Changelog subset used by the repository.
 *
 * @param {string} source
 * @returns {ParsedChangelogRelease[]}
 */
export function parseChangelog(source) {
    if (typeof source !== "string") {
        throw new TypeError("Changelog source must be a string.");
    }

    /** @type {ParsedChangelogRelease[]} */
    const releases = [];
    /** @type {ParsedChangelogRelease | null} */
    let release = null;
    /** @type {ParsedChangelogGroup | null} */
    let group = null;
    /** @type {string | null} */
    let pendingEntry = null;

    const commitEntry = () => {
        if (!pendingEntry || !group) {
            pendingEntry = null;
            return;
        }
        const normalized = normalizeEntry(pendingEntry);
        if (normalized) {
            group.entries.push(normalized);
        }
        pendingEntry = null;
    };

    const ensureGroup = () => {
        if (!release) {
            return null;
        }
        if (!group) {
            group = { label: "Highlights", entries: [] };
            release.groups.push(group);
        }
        return group;
    };

    for (const rawLine of source.replace(/^\uFEFF/, "").replaceAll("\r\n", "\n").split("\n")) {
        const releaseHeading = /^##\s+(.+?)\s*$/.exec(rawLine);
        if (releaseHeading) {
            commitEntry();
            const heading = releaseHeading[1];
            const datedHeading = /^(.*?)\s+-\s+(\d{4}-\d{2}-\d{2})$/.exec(heading);
            release = {
                version: datedHeading?.[1]?.trim() ?? heading.trim(),
                date: datedHeading?.[2] ?? null,
                groups: [],
            };
            releases.push(release);
            group = null;
            continue;
        }

        if (!release) {
            continue;
        }

        const groupHeading = /^###\s+(.+?)\s*$/.exec(rawLine);
        if (groupHeading) {
            commitEntry();
            group = { label: groupHeading[1].trim(), entries: [] };
            release.groups.push(group);
            continue;
        }

        const entry = /^-\s+(.+?)\s*$/.exec(rawLine);
        if (entry) {
            commitEntry();
            ensureGroup();
            pendingEntry = entry[1];
            continue;
        }

        if (pendingEntry && /^\s{2,}\S/.test(rawLine)) {
            pendingEntry += ` ${rawLine.trim()}`;
        } else if (rawLine.trim()) {
            commitEntry();
        }
    }

    commitEntry();
    for (const parsedRelease of releases) {
        parsedRelease.groups = parsedRelease.groups.filter((candidate) => candidate.entries.length > 0);
    }
    const populatedReleases = releases.filter((candidate) => candidate.groups.length > 0);
    const versions = new Set();
    for (const parsedRelease of populatedReleases) {
        if (versions.has(parsedRelease.version)) {
            throw new Error(`Duplicate changelog release: ${parsedRelease.version}.`);
        }
        versions.add(parsedRelease.version);
    }
    return populatedReleases;
}
