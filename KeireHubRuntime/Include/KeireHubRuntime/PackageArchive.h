#pragma once

#include "KeireHubRuntime/CatalogClient.h"
#include "KeireHubRuntime/PackageResolver.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    struct PackageArchiveLimits final
    {
        static constexpr std::uint64_t MaximumArchiveBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
        static constexpr std::uint64_t MaximumPayloadBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
        static constexpr std::uint64_t MaximumFileBytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
        static constexpr std::size_t MaximumManifestBytes = 8U * 1024U * 1024U;
        static constexpr std::size_t MaximumSignatureBytes = 16U * 1024U;
        static constexpr std::size_t MaximumFiles = 32768U;
        static constexpr std::size_t MaximumPathBytes = 1024U;
    };

    struct PackageArchiveProgress final
    {
        std::uint64_t CompletedBytes = 0;
        std::uint64_t TotalBytes = 0;
        std::string CurrentFile;
    };

    struct PackageArchiveCallbacks final
    {
        std::function<bool()> Cancelled;
        std::function<void(const PackageArchiveProgress&)> Progress;
    };

    struct PackageArchiveVerification final
    {
        // Exactly one trust source is required. Catalog manifests bind online archives; the trust store verifies
        // offline embedded signatures over the exact canonical payload-manifest bytes.
        const PackageManifest* SignedCatalogManifest = nullptr;
        const CatalogTrustStore* OfflineTrustStore = nullptr;

        // The staging directory must be a direct child of this existing, caller-authorized directory. Requiring the
        // authority separately prevents an archive or persisted operation from selecting its own write boundary.
        std::filesystem::path AllowedStagingParent;
    };

    struct PackageArchiveWriteRequest final
    {
        PackageManifest Manifest;
        std::filesystem::path PayloadRoot;
        std::filesystem::path Output;
        std::optional<DetachedSignatureMetadata> EmbeddedSignature;
        int CompressionLevel = 9;
    };

    struct PackageArchiveMetadata final
    {
        PackageManifest Manifest;
        std::optional<DetachedSignatureMetadata> EmbeddedSignature;
        std::shared_ptr<const std::vector<std::byte>> ExactManifestBytes;
        std::uint64_t ArchiveSizeBytes = 0;
        std::string ArchiveSha256;
    };

    struct PackageArchiveExtraction final
    {
        PackageArchiveMetadata Metadata;
        std::filesystem::path StagingRoot;
    };

    // The encoded payload manifest intentionally omits the archive byte size and digest: those catalog transport
    // fields cannot be embedded in the archive they describe. Every other package field is canonical and signed.
    [[nodiscard]] HubResult<std::vector<std::byte>> EncodePackageArchiveManifest(const PackageManifest& manifest);
    [[nodiscard]] HubStatus ValidatePackageTree(const std::filesystem::path& root, const PackageManifest& manifest);
    [[nodiscard]] HubStatus SealPackageTreeForPublish(const std::filesystem::path& root,
                                                      const PackageManifest& manifest);
    [[nodiscard]] HubResult<PackageArchiveMetadata> WritePackageArchive(const PackageArchiveWriteRequest& request);
    [[nodiscard]] HubResult<PackageArchiveExtraction>
    ExtractPackageArchiveToStaging(const std::filesystem::path& archive, const std::filesystem::path& stagingRoot,
                                   const PackageArchiveVerification& verification,
                                   const PackageArchiveCallbacks& callbacks = {});
} // namespace KeireHub
