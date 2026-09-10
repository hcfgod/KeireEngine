/** Keep downloadable releases separate from the version currently being developed. */
export function publicationState(sourceConfiguration: string, publication: unknown) {
    const versionPattern = /^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/;
    const sourceVersion = /^PROJECT_VERSION=(\S+)\s*$/m.exec(sourceConfiguration)?.[1];
    if (!sourceVersion || !versionPattern.test(sourceVersion)) {
        throw new Error("Project version is missing or invalid in Config/Project.conf.");
    }
    if (!publication || typeof publication !== "object") throw new Error("Publication state is missing.");
    const status = publication as Record<string, unknown>;
    if (status.state !== "active" && status.state !== "preparing") throw new Error("Unknown publication state.");
    const availableVersion = status.state === "active" ? status.version : status.activeCatalogVersion;
    if (typeof status.version !== "string" || !versionPattern.test(status.version) ||
        typeof availableVersion !== "string" || !versionPattern.test(availableVersion)) {
        throw new Error("Publication versions are missing or invalid.");
    }
    return { sourceVersion, publicationVersion: status.version, preparing: status.state === "preparing", availableVersion };
}
