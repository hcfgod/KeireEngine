#pragma once

#include "KeireHubRuntime/InstallTransaction.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub::Detail
{
    inline constexpr std::uint64_t MaximumInstallFileBytes = 1024ULL * 1024ULL * 1024ULL * 1024ULL;

    struct InstallerPackageManifest final
    {
        std::string ProductId;
        InstallProduct Product = InstallProduct::Editor;
        std::string Version;
        std::string BuildIdentity;
        std::string Fingerprint;
        std::filesystem::path ManifestPath;
        std::vector<InstallOwnedFile> Files;
    };

    struct InstallMarker final
    {
        std::string ProductId;
        std::string InstallationId;
        InstallProduct Product = InstallProduct::Editor;
        std::string ManifestFingerprint;
    };

    [[nodiscard]] std::string NormalizedInstallPathKey(const std::filesystem::path& path);
    [[nodiscard]] HubResult<InstallerPackageManifest>
    ReadInstallerPackageManifest(const std::filesystem::path& sourceRoot, InstallProduct product,
                                 bool allowUnknownFiles = false);
    [[nodiscard]] HubResult<std::string> EncodeInstallMarker(const InstallMarker& marker);
    [[nodiscard]] HubResult<InstallMarker> ReadInstallMarker(const std::filesystem::path& root);
    [[nodiscard]] HubStatus ValidateInstallTree(const std::filesystem::path& root, bool allowMissing);
    // Rechecks every existing component without following links immediately before a filesystem mutation. The leaf
    // may be absent only when allowMissingLeaf is true; an existing leaf must be an ordinary file or directory.
    [[nodiscard]] HubStatus ValidateInstallMutationPath(const std::filesystem::path& root,
                                                        const std::filesystem::path& relative, bool allowMissingLeaf);
    [[nodiscard]] HubResult<std::string> HashInstallFile(const std::filesystem::path& path);
    [[nodiscard]] std::string HashInstallDocument(std::string_view document);
    [[nodiscard]] HubResult<std::optional<InstallRegistration>>
    ReadPendingInstallPreviousRegistration(const std::filesystem::path& destination, InstallProduct product);
} // namespace KeireHub::Detail
