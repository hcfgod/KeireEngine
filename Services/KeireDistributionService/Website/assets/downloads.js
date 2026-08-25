const channel = "stable";
const previewMetadataPath = "/assets/preview-downloads.json";
const hosts = [
    ["windows", "x86_64"],
    ["windows", "arm64"],
    ["macos", "x86_64"],
    ["macos", "arm64"],
    ["linux", "x86_64"],
    ["linux", "arm64"],
];
const sha256Pattern = /^[0-9a-f]{64}$/;
const identityPattern = /^[a-z0-9][a-z0-9._-]{0,63}$/;
const keyIdPattern = /^ed25519-[0-9a-f]{32}$/;
const utcTimestampPattern = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,9})?(?:Z|\+00:00)$/;
const installerNamePattern = /^[A-Za-z0-9][A-Za-z0-9._ -]{0,127}$/;
const installerFormats = {
    windows: { exe: { suffix: ".exe", label: "EXE", audience: "Windows" } },
    macos: { dmg: { suffix: ".dmg", label: "DMG", audience: "macOS" } },
    linux: {
        deb: { suffix: ".deb", label: "DEB", audience: "Ubuntu/Debian" },
        rpm: { suffix: ".rpm", label: "RPM", audience: "Rocky/Fedora/openSUSE" },
    },
};
const defaultInstallerFormats = {
    windows: "exe",
    macos: "dmg",
    linux: "deb",
};
const compactInstallerNames = {
    windows: { exe: "KeireHubSetup.exe" },
    macos: { dmg: "KeireHub.dmg" },
    linux: { deb: "keire-hub.deb", rpm: "keire-hub.rpm" },
};
let previewReleaseStatus = null;

function semanticVersion(value) {
    const match = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$/.exec(value);
    if (!match) {
        return null;
    }
    const prerelease = match[4] ? match[4].split(".") : [];
    if (prerelease.some((identifier) => /^\d+$/.test(identifier) && identifier.length > 1 && identifier[0] === "0")) {
        return null;
    }
    return {
        raw: value,
        core: [match[1], match[2], match[3]],
        prerelease,
    };
}

function compareNumericIdentifiers(left, right) {
    if (left.length !== right.length) {
        return left.length - right.length;
    }
    return left.localeCompare(right);
}

function compareVersions(left, right) {
    for (let index = 0; index < 3; ++index) {
        if (left.core[index] !== right.core[index]) {
            return compareNumericIdentifiers(left.core[index], right.core[index]);
        }
    }
    if (left.prerelease.length === 0 || right.prerelease.length === 0) {
        return right.prerelease.length - left.prerelease.length;
    }
    const count = Math.max(left.prerelease.length, right.prerelease.length);
    for (let index = 0; index < count; ++index) {
        const leftValue = left.prerelease[index];
        const rightValue = right.prerelease[index];
        if (leftValue === undefined || rightValue === undefined) {
            return leftValue === undefined ? -1 : 1;
        }
        if (leftValue === rightValue) {
            continue;
        }
        const leftNumber = /^\d+$/.test(leftValue);
        const rightNumber = /^\d+$/.test(rightValue);
        if (leftNumber && rightNumber) {
            return compareNumericIdentifiers(leftValue, rightValue);
        }
        if (leftNumber !== rightNumber) {
            return leftNumber ? -1 : 1;
        }
        return leftValue.localeCompare(rightValue);
    }
    return 0;
}

function comparePreviewCandidates(left, right) {
    const versionOrder = compareVersions(right.version, left.version);
    if (versionOrder !== 0) {
        return versionOrder;
    }
    const publicationOrder = Date.parse(right.packageRecord.publishedAt) - Date.parse(left.packageRecord.publishedAt);
    if (publicationOrder !== 0) {
        return publicationOrder;
    }
    return left.installerFormat.localeCompare(right.installerFormat);
}

function previewInstallerFormat(packageRecord) {
    const formats = installerFormats[packageRecord?.platform];
    if (!formats || typeof packageRecord?.fileName !== "string") {
        return null;
    }
    let format = packageRecord.packageFormat;
    if (format !== undefined && typeof format !== "string") {
        return null;
    }
    if (format === undefined) {
        format = Object.keys(formats).find((candidate) =>
            packageRecord.fileName.toLowerCase().endsWith(formats[candidate].suffix));
    }
    const descriptor = formats[format];
    return descriptor && packageRecord.fileName.toLowerCase().endsWith(descriptor.suffix) ? format : null;
}

function catalogInstallerFormat(packageRecord, platform, catalogSchema) {
    const formats = installerFormats[platform];
    if (!formats) {
        return null;
    }
    if (packageRecord.packageFormat !== undefined) {
        return typeof packageRecord.packageFormat === "string" && formats[packageRecord.packageFormat] ?
            packageRecord.packageFormat : null;
    }
    if (catalogSchema === 1 && Array.isArray(packageRecord.files) && packageRecord.files.length === 1) {
        const path = packageRecord.files[0]?.path?.toLowerCase();
        return Object.keys(formats).find((candidate) => path?.endsWith(formats[candidate].suffix)) ?? null;
    }
    return defaultInstallerFormats[platform] ?? null;
}

function safeInstallerName(packageRecord, platform, artifact, catalogSchema, installerFormat) {
    if (catalogSchema === 2) {
        const manifest = packageRecord.manifest;
        if (!manifest || !Number.isSafeInteger(manifest.sizeBytes) || manifest.sizeBytes < 1 ||
            typeof manifest.sha256 !== "string" || !sha256Pattern.test(manifest.sha256) ||
            Object.hasOwn(packageRecord, "files")) {
            return null;
        }
        return compactInstallerNames[platform]?.[installerFormat] ?? null;
    }
    if (!Array.isArray(packageRecord.files) || packageRecord.files.length !== 1) {
        return null;
    }
    const file = packageRecord.files[0];
    const path = file?.path;
    if (typeof path !== "string" || !installerNamePattern.test(path) || path.includes("..") ||
        file.sizeBytes !== artifact?.sizeBytes || file.sha256 !== artifact?.sha256 || file.mode !== 420) {
        return null;
    }
    if (!path.toLowerCase().endsWith(installerFormats[platform][installerFormat].suffix)) {
        return null;
    }
    return path;
}

function validateCatalog(catalog, platform, architecture) {
    if (!catalog || ![1, 2].includes(catalog.schemaVersion) || catalog.channel !== channel ||
        catalog.platform !== platform ||
        catalog.architecture !== architecture || typeof catalog.keyId !== "string" || !keyIdPattern.test(catalog.keyId) ||
        !Number.isSafeInteger(catalog.sequence) || catalog.sequence < 1 ||
        typeof catalog.expiresAt !== "string" || !utcTimestampPattern.test(catalog.expiresAt) ||
        !Number.isFinite(Date.parse(catalog.expiresAt)) ||
        Date.parse(catalog.expiresAt) <= Date.now() || !Array.isArray(catalog.packages)) {
        return [];
    }
    const candidates = [];
    for (const packageRecord of catalog.packages) {
        const version = semanticVersion(packageRecord?.version);
        const artifact = packageRecord?.artifact;
        const installerFormat = catalogInstallerFormat(packageRecord, platform, catalog.schemaVersion);
        const installerName = installerFormat ?
            safeInstallerName(packageRecord, platform, artifact, catalog.schemaVersion, installerFormat) : null;
        if (packageRecord?.schemaVersion !== 1 || packageRecord?.type !== "hubInstaller" || !version ||
            packageRecord.channel !== channel || packageRecord.platform !== platform ||
            packageRecord.architecture !== architecture || packageRecord.signatureKeyId !== catalog.keyId ||
            typeof packageRecord.packageId !== "string" || !identityPattern.test(packageRecord.packageId) ||
            typeof packageRecord.displayName !== "string" || packageRecord.displayName.trim().length === 0 ||
            !artifact || !Number.isSafeInteger(artifact.sizeBytes) || artifact.sizeBytes < 1 ||
            typeof artifact.sha256 !== "string" || !sha256Pattern.test(artifact.sha256) || !installerName) {
            continue;
        }
        candidates.push({ packageRecord, version, installerName, installerFormat });
    }
    candidates.sort((left, right) => compareVersions(right.version, left.version));
    return candidates;
}

function validatePreviewMetadata(metadata) {
    if (!metadata || metadata.schemaVersion !== 2 || !Array.isArray(metadata.packages)) {
        return [];
    }
    const candidates = [];
    for (const packageRecord of metadata.packages) {
        const version = semanticVersion(packageRecord?.version);
        const editorVersion = semanticVersion(packageRecord?.editorVersion);
        const installerFormat = previewInstallerFormat(packageRecord);
        const expectedUrl = `/preview-downloads/${packageRecord?.fileName}`;
        if (packageRecord?.type !== "hubInstallerPreview" || !version || !editorVersion ||
            typeof packageRecord.releaseId !== "string" || !identityPattern.test(packageRecord.releaseId) ||
            typeof packageRecord.publishedAt !== "string" || !utcTimestampPattern.test(packageRecord.publishedAt) ||
            !Number.isFinite(Date.parse(packageRecord.publishedAt)) || packageRecord.signed !== false ||
            packageRecord.developmentArtifact !== true || !hosts.some(([platform, architecture]) =>
                platform === packageRecord.platform && architecture === packageRecord.architecture) ||
            typeof packageRecord.fileName !== "string" || !installerNamePattern.test(packageRecord.fileName) ||
            packageRecord.fileName.includes("..") || !installerFormat || packageRecord.url !== expectedUrl ||
            !Number.isSafeInteger(packageRecord.sizeBytes) || packageRecord.sizeBytes < 1 ||
            typeof packageRecord.sha256 !== "string" || !sha256Pattern.test(packageRecord.sha256) ||
            !packageRecord.fileName.includes(packageRecord.sha256.slice(0, 8))) {
            continue;
        }
        candidates.push({ packageRecord, version, editorVersion, installerFormat });
    }
    candidates.sort(comparePreviewCandidates);
    const retainedIdentities = new Set();
    return candidates.filter(({ packageRecord, installerFormat }) => {
        const identity = `${packageRecord.platform}/${packageRecord.architecture}/${packageRecord.version}/${installerFormat}`;
        if (retainedIdentities.has(identity)) {
            return false;
        }
        retainedIdentities.add(identity);
        return true;
    });
}

function validatePreviewReleaseStatus(metadata) {
    const status = metadata?.releaseStatus;
    const targetVersion = semanticVersion(status?.version);
    const activeCatalogVersion = semanticVersion(status?.activeCatalogVersion);
    if (!targetVersion || typeof status.message !== "string" ||
        status.message.length < 20 || status.message.length > 240) {
        return null;
    }
    if (status.state === "preparing") {
        return activeCatalogVersion && compareVersions(targetVersion, activeCatalogVersion) > 0 ? status : null;
    }
    return status.state === "active" && status.activeCatalogVersion === status.version ? status : null;
}

function releaseCandidates(candidates, version) {
    if (!semanticVersion(version)) {
        return [];
    }
    return candidates.filter((candidate) => candidate.version.raw === version);
}

function bytes(value) {
    const units = ["B", "KB", "MB", "GB"];
    let amount = value;
    let unit = 0;
    while (amount >= 1024 && unit < units.length - 1) {
        amount /= 1024;
        ++unit;
    }
    return `${amount.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function architectureLabel(value) {
    return value === "x86_64" ? "x86-64" : "ARM64";
}

function platformLabel(value) {
    return { windows: "Windows", macos: "macOS", linux: "Linux" }[value] ?? value;
}

function publishedTimestamp(value) {
    const instant = new Date(value);
    const timestamp = element("time", "published-time", new Intl.DateTimeFormat(undefined, {
        year: "numeric",
        month: "short",
        day: "numeric",
        hour: "numeric",
        minute: "2-digit",
        timeZoneName: "short",
    }).format(instant));
    timestamp.dateTime = instant.toISOString();
    timestamp.title = `Published at ${instant.toISOString()} (UTC)`;
    return timestamp;
}

function element(name, className, text) {
    const result = document.createElement(name);
    if (className) {
        result.className = className;
    }
    if (text !== undefined) {
        result.textContent = text;
    }
    return result;
}

function renderVariant(target, platform, architecture, candidate) {
    const variant = element("section", "download-variant");
    const format = installerFormats[platform][candidate.installerFormat];
    const header = element("div", "variant-row");
    header.append(element("strong", "", `${architectureLabel(architecture)} · ${format.label}`));
    header.append(element("span", "", `${bytes(candidate.packageRecord.artifact.sizeBytes)}`));
    variant.append(header);
    variant.append(element("p", "version-detail", `Hub v${candidate.version.raw} · Verified by signed Kéire catalog`));

    const download = element("a", "button button-primary", `Download ${format.label}`);
    download.href = `/v1/packages/${candidate.packageRecord.artifact.sha256}`;
    download.download = candidate.installerName;
    download.setAttribute("aria-label", `Download Kéire Hub ${candidate.version.raw} ${format.label} for ${platform} ${architectureLabel(architecture)}`);
    variant.append(download);

    const checksumRow = element("div", "checksum-row");
    const checksum = element("code", "checksum", candidate.packageRecord.artifact.sha256);
    checksum.title = candidate.packageRecord.artifact.sha256;
    const copy = element("button", "copy-button", "Copy SHA-256");
    copy.type = "button";
    copy.addEventListener("click", async () => {
        try {
            await navigator.clipboard.writeText(candidate.packageRecord.artifact.sha256);
            copy.textContent = "Copied";
            window.setTimeout(() => { copy.textContent = "Copy SHA-256"; }, 1800);
        } catch {
            copy.textContent = "Copy failed";
        }
    });
    checksumRow.append(checksum, copy);
    variant.append(checksumRow);
    target.append(variant);
}

function renderPreviewVariant(target, candidate) {
    const record = candidate.packageRecord;
    const format = installerFormats[record.platform][candidate.installerFormat];
    const variant = element("section", "download-variant preview-variant");
    variant.append(element("span", "preview-label", "Unsigned development preview"));
    const header = element("div", "variant-row");
    header.append(element("strong", "", `${architectureLabel(record.architecture)} · ${format.label}`));
    header.append(element("span", "", bytes(record.sizeBytes)));
    variant.append(header);
    const versionDetail = element("p", "version-detail");
    versionDetail.append(`Hub v${candidate.version.raw} · Editor v${candidate.editorVersion.raw} · ${format.audience} · Published `);
    versionDetail.append(publishedTimestamp(record.publishedAt));
    variant.append(versionDetail);

    const download = element("a", "button button-primary", `Download ${format.label} preview`);
    download.href = record.url;
    download.download = record.fileName;
    download.setAttribute("aria-label", `Download unsigned Kéire Hub ${candidate.version.raw} ${format.label} development preview for ${platformLabel(record.platform)} ${architectureLabel(record.architecture)} with editor ${candidate.editorVersion.raw}`);
    variant.append(download);
    const warning = record.platform === "windows" ?
        "Unsigned preview: verify the SHA-256 below and expect a Windows publisher warning." :
        "Unsigned preview: verify the SHA-256 below before installing this package.";
    variant.append(element("p", "preview-warning", warning));

    const checksumRow = element("div", "checksum-row");
    const checksum = element("code", "checksum", record.sha256);
    checksum.title = record.sha256;
    const copy = element("button", "copy-button", "Copy SHA-256");
    copy.type = "button";
    copy.addEventListener("click", async () => {
        try {
            await navigator.clipboard.writeText(record.sha256);
            copy.textContent = "Copied";
            window.setTimeout(() => { copy.textContent = "Copy SHA-256"; }, 1800);
        } catch {
            copy.textContent = "Copy failed";
        }
    });
    checksumRow.append(checksum, copy);
    variant.append(checksumRow);
    target.append(variant);
}

function detectedPlatform() {
    const source = `${navigator.userAgent} ${navigator.platform}`.toLowerCase();
    if (source.includes("win")) {
        return "windows";
    }
    if (source.includes("mac")) {
        return "macos";
    }
    if (source.includes("linux")) {
        return "linux";
    }
    return null;
}

async function loadCatalog(platform, architecture) {
    const response = await fetch(`/v2/catalog/${channel}/${platform}/${architecture}`, {
        headers: { Accept: "application/json" },
        cache: "no-cache",
    });
    if (response.status === 404) {
        return [];
    }
    if (!response.ok) {
        throw new Error(`Catalog returned ${response.status}.`);
    }
    return validateCatalog(await response.json(), platform, architecture);
}

async function loadPreviewDownloads() {
    const response = await fetch(previewMetadataPath, {
        headers: { Accept: "application/json" },
        cache: "no-cache",
    });
    if (!response.ok) {
        return [];
    }
    const metadata = await response.json();
    previewReleaseStatus = validatePreviewReleaseStatus(metadata);
    const candidates = validatePreviewMetadata(metadata);
    const available = [];
    let next = 0;
    async function validateNext() {
        while (next < candidates.length) {
            const candidate = candidates[next++];
            try {
                const artifactResponse = await fetch(candidate.packageRecord.url, { method: "HEAD", cache: "no-cache" });
                const contentLength = Number(artifactResponse.headers.get("content-length"));
                if (artifactResponse.ok && Number.isSafeInteger(contentLength) &&
                    contentLength === candidate.packageRecord.sizeBytes) {
                    available.push(candidate);
                }
            } catch {
                // A missing preview is omitted without hiding other retained releases.
            }
        }
    }
    await Promise.all(Array.from({ length: Math.min(6, candidates.length) }, validateNext));
    available.sort(comparePreviewCandidates);
    return available;
}

async function loadDownloads() {
    const platform = detectedPlatform();
    if (platform) {
        document.querySelector(`[data-platform="${platform}"]`)?.classList.add("recommended");
    }
    const results = await Promise.allSettled(hosts.map(async ([hostPlatform, architecture]) => ({
        platform: hostPlatform,
        architecture,
        candidates: await loadCatalog(hostPlatform, architecture),
    })));
    const previews = await loadPreviewDownloads().catch(() => []);
    const catalogVersion = previewReleaseStatus?.state === "preparing" ?
        previewReleaseStatus.activeCatalogVersion : previewReleaseStatus?.version;
    for (const hostPlatform of ["windows", "macos", "linux"]) {
        const card = document.querySelector(`[data-platform="${hostPlatform}"]`);
        if (!(card instanceof HTMLElement)) {
            continue;
        }
        const state = card.querySelector("[data-download-state]");
        const variants = card.querySelector("[data-download-variants]");
        if (!(state instanceof HTMLElement) || !(variants instanceof HTMLElement)) {
            continue;
        }
        const matching = results.filter((result) => result.status === "fulfilled" && result.value.platform === hostPlatform);
        for (const result of matching) {
            const eligibleCandidates = releaseCandidates(result.value.candidates, catalogVersion);
            const latestVersion = eligibleCandidates[0]?.version.raw;
            for (const candidate of eligibleCandidates.filter((value) => value.version.raw === latestVersion)) {
                renderVariant(variants, hostPlatform, result.value.architecture, candidate);
            }
        }
        const platformPreviews = releaseCandidates(
            previews.filter((candidate) => candidate.packageRecord.platform === hostPlatform),
            previewReleaseStatus?.version);
        const latestPreviewVersion = platformPreviews[0]?.version.raw;
        const currentPreviews = platformPreviews.filter((candidate) => candidate.version.raw === latestPreviewVersion);
        if (variants.children.length === 0 && currentPreviews.length > 0) {
            for (const preview of currentPreviews) {
                renderPreviewVariant(variants, preview);
            }
            state.textContent = platform === hostPlatform ?
                "Recommended for this device. Development preview available:" :
                "Development preview available:";
        } else if (variants.children.length > 0) {
            state.textContent = platform === hostPlatform ?
                "Recommended for this device. Catalog-verified releases:" : "Catalog-verified releases:";
        } else if (previewReleaseStatus) {
            state.textContent = previewReleaseStatus.message;
        } else if (matching.length < 2) {
            state.textContent = "Catalog service is temporarily unavailable. No download link is being shown.";
        } else {
            state.textContent = "A signed stable Hub installer has not been published for this platform yet.";
        }
    }
}

async function loadHistory() {
    const history = document.querySelector("[data-download-history]");
    if (!(history instanceof HTMLElement)) {
        return;
    }
    const results = await Promise.allSettled(hosts.map(async ([platform, architecture]) => ({
        platform,
        architecture,
        candidates: await loadCatalog(platform, architecture),
    })));
    const previews = await loadPreviewDownloads().catch(() => []);
    for (const platform of ["windows", "linux", "macos"]) {
        const target = document.querySelector(`[data-history-platform="${platform}"]`);
        const state = document.querySelector(`[data-history-state="${platform}"]`);
        if (!(target instanceof HTMLElement) || !(state instanceof HTMLElement)) {
            continue;
        }
        let count = 0;
        for (const result of results) {
            if (result.status !== "fulfilled" || result.value.platform !== platform) {
                continue;
            }
            for (const candidate of result.value.candidates) {
                renderVariant(target, platform, result.value.architecture, candidate);
                ++count;
            }
        }
        for (const candidate of previews.filter((item) => item.packageRecord.platform === platform)) {
            renderPreviewVariant(target, candidate);
            ++count;
        }
        state.textContent = count === 0 ?
            (previewReleaseStatus?.message ?? "No retained releases are available for this platform yet.") :
            `${count} retained ${count === 1 ? "release" : "releases"}`;
    }
}

const pageLoader = document.querySelector("[data-download-history]") ? loadHistory : loadDownloads;
pageLoader().catch(() => {
    for (const state of document.querySelectorAll("[data-download-state]")) {
        state.textContent = "Catalog service is temporarily unavailable. No download link is being shown.";
    }
    for (const state of document.querySelectorAll("[data-history-state]")) {
        state.textContent = "Release history is temporarily unavailable.";
    }
});
