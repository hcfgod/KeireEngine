#pragma once

#include "KeireHubRuntime/PackageArchive.h"
#include "KeireHubRuntime/PackageReceipt.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace KeireHub
{
    class PackageTreeSeal final
    {
      public:
        [[nodiscard]] std::span<const PackageFile> Files() const noexcept { return m_Files; }
        [[nodiscard]] std::uint64_t InstalledSizeBytes() const noexcept { return m_InstalledSizeBytes; }
        [[nodiscard]] const std::string& IdentitySha256() const noexcept { return m_IdentitySha256; }
        [[nodiscard]] std::span<const std::string> PackageIdentities() const noexcept { return m_PackageIdentities; }

      private:
        friend HubResult<PackageTreeSeal> CreatePackageTreeSeal(std::span<const PackageManifest> manifests);

        std::vector<PackageFile> m_Files;
        std::uint64_t m_InstalledSizeBytes = 0;
        std::string m_IdentitySha256;
        std::vector<std::string> m_PackageIdentities;
    };

    struct PackageAssemblySource final
    {
        std::filesystem::path Root;
        PackageManifest Manifest;
    };

    struct PackageAssemblyRequest final
    {
        std::span<const PackageAssemblySource> Sources;
        std::filesystem::path AllowedStagingParent;
        std::filesystem::path StagingRoot;
        PackageArchiveCallbacks Callbacks;
    };

    struct PackageAssemblyResult final
    {
        PackageTreeSeal Seal;
        // Internal aggregate used only to validate and recover atomic publication of the resolved package set. It is
        // derived from the editor manifest and is not a signed distribution manifest.
        PackageManifest PublicationManifest;
        std::filesystem::path StagingRoot;
    };

    [[nodiscard]] HubResult<PackageTreeSeal> CreatePackageTreeSeal(std::span<const PackageManifest> manifests);
    // The first manifest is the editor package whose product metadata identifies the installed tree. Remaining
    // manifests are resolved components. The result is deterministic for any ordering of those remaining manifests.
    [[nodiscard]] HubResult<PackageManifest>
    CreatePackagePublicationManifest(std::span<const PackageManifest> manifests);
    [[nodiscard]] HubStatus ValidatePackageTree(const std::filesystem::path& root, const PackageTreeSeal& seal);
    [[nodiscard]] HubStatus SealPackageTreeForPublish(const std::filesystem::path& root, const PackageTreeSeal& seal);
    [[nodiscard]] HubResult<PackageAssemblyResult> AssemblePackageTreesToStaging(const PackageAssemblyRequest& request);
    [[nodiscard]] HubResult<PackageManifest>
    FinalizePackageAssemblyReceipt(const std::filesystem::path& stagingRoot, const PackageManifest& publicationManifest,
                                   std::span<const PackageManifest> sourceManifests);
    [[nodiscard]] HubResult<PackageManifest> FinalizePackageAssemblyMarker(const std::filesystem::path& stagingRoot,
                                                                           const PackageManifest& publicationManifest);
} // namespace KeireHub
