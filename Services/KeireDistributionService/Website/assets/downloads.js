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
const fileNames = {
    windows: ".exe",
    macos: ".dmg",
    linux: ".deb",
};
const compactInstallerNames = {
    windows: "KeireHubSetup.exe",
    macos: "KeireHub.dmg",
    linux: "keire-hub.deb",
};

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
    return Date.parse(right.packageRecord.publishedAt) - Date.parse(left.packageRecord.publishedAt);
}

function safeInstallerName(packageRecord, platform, artifact, catalogSchema) {
    if (catalogSchema === 2) {
        const manifest = packageRecord.manifest;
        if (!manifest || !Number.isSafeInteger(manifest.sizeBytes) || manifest.sizeBytes < 1 ||
            typeof manifest.sha256 !== "string" || !sha256Pattern.test(manifest.sha256) ||
            Object.hasOwn(packageRecord, "files")) {
            return null;
        }
        return compactInstallerNames[platform] ?? null;
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
    if (!path.toLowerCase().endsWith(fileNames[platform])) {
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
        const installerName = safeInstallerName(packageRecord, platform, artifact, catalog.schemaVersion);
        if (packageRecord?.schemaVersion !== 1 || packageRecord?.type !== "hubInstaller" || !version ||
            packageRecord.channel !== channel || packageRecord.platform !== platform ||
            packageRecord.architecture !== architecture || packageRecord.signatureKeyId !== catalog.keyId ||
            typeof packageRecord.packageId !== "string" || !identityPattern.test(packageRecord.packageId) ||
            typeof packageRecord.displayName !== "string" || packageRecord.displayName.trim().length === 0 ||
            !artifact || !Number.isSafeInteger(artifact.sizeBytes) || artifact.sizeBytes < 1 ||
            typeof artifact.sha256 !== "string" || !sha256Pattern.test(artifact.sha256) || !installerName) {
            continue;
        }
        candidates.push({ packageRecord, version, installerName });
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
        const expectedSuffix = fileNames[packageRecord?.platform];
        const expectedUrl = `/preview-downloads/${packageRecord?.fileName}`;
        if (packageRecord?.type !== "hubInstallerPreview" || !version || !editorVersion ||
            typeof packageRecord.releaseId !== "string" || !identityPattern.test(packageRecord.releaseId) ||
            typeof packageRecord.publishedAt !== "string" || !utcTimestampPattern.test(packageRecord.publishedAt) ||
            !Number.isFinite(Date.parse(packageRecord.publishedAt)) || packageRecord.signed !== false ||
            packageRecord.developmentArtifact !== true || !hosts.some(([platform, architecture]) =>
                platform === packageRecord.platform && architecture === packageRecord.architecture) ||
            typeof packageRecord.fileName !== "string" || !installerNamePattern.test(packageRecord.fileName) ||
            packageRecord.fileName.includes("..") || !expectedSuffix ||
            !packageRecord.fileName.toLowerCase().endsWith(expectedSuffix) || packageRecord.url !== expectedUrl ||
            !Number.isSafeInteger(packageRecord.sizeBytes) || packageRecord.sizeBytes < 1 ||
            typeof packageRecord.sha256 !== "string" || !sha256Pattern.test(packageRecord.sha256) ||
            !packageRecord.fileName.includes(packageRecord.sha256.slice(0, 8))) {
            continue;
        }
        candidates.push({ packageRecord, version, editorVersion });
    }
    candidates.sort(comparePreviewCandidates);
    const retainedIdentities = new Set();
    return candidates.filter(({ packageRecord }) => {
        const identity = `${packageRecord.platform}/${packageRecord.architecture}/${packageRecord.version}`;
        if (retainedIdentities.has(identity)) {
            return false;
        }
        retainedIdentities.add(identity);
        return true;
    });
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
    const header = element("div", "variant-row");
    header.append(element("strong", "", architectureLabel(architecture)));
    header.append(element("span", "", `${bytes(candidate.packageRecord.artifact.sizeBytes)}`));
    variant.append(header);
    variant.append(element("p", "version-detail", `Hub v${candidate.version.raw} · Editor versions from signed catalog`));

    const download = element("a", "button button-primary", `Download ${architectureLabel(architecture)}`);
    download.href = `/v1/packages/${candidate.packageRecord.artifact.sha256}`;
    download.download = candidate.installerName;
    download.setAttribute("aria-label", `Download Kéire Hub ${candidate.version.raw} for ${platform} ${architectureLabel(architecture)}`);
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
    const variant = element("section", "download-variant preview-variant");
    variant.append(element("span", "preview-label", "Unsigned development preview"));
    const header = element("div", "variant-row");
    header.append(element("strong", "", architectureLabel(record.architecture)));
    header.append(element("span", "", bytes(record.sizeBytes)));
    variant.append(header);
    const versionDetail = element("p", "version-detail");
    versionDetail.append(`Hub v${candidate.version.raw} · Editor v${candidate.editorVersion.raw} · Published `);
    versionDetail.append(publishedTimestamp(record.publishedAt));
    variant.append(versionDetail);

    const download = element("a", "button button-primary", `Download ${architectureLabel(record.architecture)} preview`);
    download.href = record.url;
    download.download = record.fileName;
    download.setAttribute("aria-label", `Download unsigned Kéire Hub ${candidate.version.raw} development preview for ${platformLabel(record.platform)} ${architectureLabel(record.architecture)} with editor ${candidate.editorVersion.raw}`);
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
    const candidates = validatePreviewMetadata(await response.json());
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
            const candidate = result.value.candidates[0];
            if (candidate) {
                renderVariant(variants, hostPlatform, result.value.architecture, candidate);
            }
        }
        const preview = previews.find((candidate) => candidate.packageRecord.platform === hostPlatform);
        if (variants.children.length === 0 && preview) {
            renderPreviewVariant(variants, preview);
            state.textContent = platform === hostPlatform ?
                "Recommended for this device. Development preview available:" :
                "Development preview available:";
        } else if (variants.children.length > 0) {
            state.textContent = platform === hostPlatform ? "Recommended for this device. Signed stable releases:" : "Signed stable releases:";
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
            "No retained releases are available for this platform yet." :
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
