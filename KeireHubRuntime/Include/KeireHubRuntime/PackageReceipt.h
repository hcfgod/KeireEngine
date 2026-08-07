#pragma once

#include "KeireHubRuntime/PackageResolver.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace KeireHub
{
    inline constexpr const char* PackageInstallReceiptFileName = ".keirehub-packages.json";

    struct InstalledPackageRecord final
    {
        std::string Id;
        SemanticVersion Version;
        PackageKind Kind = PackageKind::Editor;
        std::uint64_t ArtifactSizeBytes = 0;
        std::string ArtifactSha256;
        std::uint64_t InstalledSizeBytes = 0;
        std::vector<PackageDependency> Dependencies;
        std::vector<PackageFile> Files;
        std::vector<std::string> LicenseReferences;
    };

    struct PackageInstallReceipt final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        // Populated by ReadPackageInstallReceipt from the exact persisted bytes; it is not encoded into the receipt.
        std::string DocumentSha256;
        // This identity covers the exact verified source manifests. Receipt and ownership-marker bytes are excluded
        // to avoid a self-referential digest; the finalized publication manifest covers the complete installed tree.
        std::string AggregateIdentitySha256;
        std::uint64_t AggregateInstalledSizeBytes = 0;
        std::vector<InstalledPackageRecord> Packages;
    };

    [[nodiscard]] HubStatus ValidatePackageInstallReceipt(const PackageInstallReceipt& receipt);
    [[nodiscard]] HubResult<std::string> EncodePackageInstallReceipt(const PackageInstallReceipt& receipt);
    [[nodiscard]] HubResult<PackageInstallReceipt>
    ReadPackageInstallReceipt(const std::filesystem::path& installationRoot);
} // namespace KeireHub
