#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class AssetPackageInstallKind : std::uint8_t
    {
        Registry,
        AssetImport,
        CompleteProject
    };

    enum class AssetPackageManagedAssemblyScope : std::uint8_t
    {
        Runtime,
        Editor,
        Test
    };

    struct AssetPackageCompatibility
    {
        std::string MinimumEngineVersion;
        std::optional<std::string> MaximumEngineVersion;
        std::vector<std::string> Platforms;
        std::vector<std::string> Architectures;
        std::vector<std::string> RendererCapabilities;
        std::string ManagedApiVersion;

        [[nodiscard]] bool operator==(const AssetPackageCompatibility&) const = default;
    };

    struct AssetPackageDependency
    {
        std::string PackageId;
        std::string VersionRange;

        [[nodiscard]] bool operator==(const AssetPackageDependency&) const = default;
    };

    struct AssetPackageConflict
    {
        std::string PackageId;
        std::string VersionRange;

        [[nodiscard]] bool operator==(const AssetPackageConflict&) const = default;
    };

    struct AssetPackageFile
    {
        std::filesystem::path Path;
        std::uint64_t SizeBytes = 0;
        std::string Sha256;
        std::uint32_t Mode = 0644U;

        [[nodiscard]] bool operator==(const AssetPackageFile&) const = default;
    };

    struct AssetPackageAsset
    {
        AssetId Id;
        AssetTypeId Type;
        std::filesystem::path SourcePath;
        std::filesystem::path MetadataPath;
        std::vector<AssetId> Dependencies;

        [[nodiscard]] bool operator==(const AssetPackageAsset&) const = default;
    };

    struct AssetPackageSample
    {
        std::string Id;
        std::string DisplayName;
        std::string Description;
        std::filesystem::path Root;

        [[nodiscard]] bool operator==(const AssetPackageSample&) const = default;
    };

    struct AssetPackageManagedAssembly
    {
        std::string Name;
        std::filesystem::path DefinitionPath;
        AssetPackageManagedAssemblyScope Scope = AssetPackageManagedAssemblyScope::Runtime;

        [[nodiscard]] bool operator==(const AssetPackageManagedAssembly&) const = default;
    };

    struct AssetPackageLicense
    {
        std::string Id;
        std::filesystem::path Path;

        [[nodiscard]] bool operator==(const AssetPackageLicense&) const = default;
    };

    struct AssetPackageManifest
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        std::string PackageId;
        std::string Version;
        std::string PublisherId;
        std::string DisplayName;
        std::string Summary;
        std::string Channel = "stable";
        AssetPackageInstallKind InstallKind = AssetPackageInstallKind::Registry;
        AssetPackageCompatibility Compatibility;
        std::vector<AssetPackageDependency> Dependencies;
        std::vector<AssetPackageConflict> Conflicts;
        std::vector<AssetPackageFile> Files;
        std::vector<AssetPackageAsset> Assets;
        std::vector<AssetPackageSample> Samples;
        std::vector<AssetPackageManagedAssembly> ManagedAssemblies;
        std::vector<AssetPackageLicense> Licenses;
        std::vector<std::string> EntryPoints;
        std::uint64_t InstalledSizeBytes = 0;
        std::string SignatureKeyId;

        [[nodiscard]] bool operator==(const AssetPackageManifest&) const = default;
    };

    struct AssetPackageSignature
    {
        std::string Algorithm = "ed25519";
        std::string KeyId;
        std::vector<std::byte> Bytes;

        [[nodiscard]] bool operator==(const AssetPackageSignature&) const = default;
    };

    struct AssetPackageArchiveLimits
    {
        static constexpr std::uint64_t MaximumArchiveBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
        static constexpr std::uint64_t MaximumPayloadBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
        static constexpr std::uint64_t MaximumFileBytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
        static constexpr std::size_t MaximumManifestBytes = std::size_t{8U} * 1024U * 1024U;
        static constexpr std::size_t MaximumSignatureBytes = std::size_t{16U} * 1024U;
        static constexpr std::size_t MaximumFiles = 65536U;
        static constexpr std::size_t MaximumAssets = 65536U;
        static constexpr std::size_t MaximumPathBytes = 1024U;
    };

    struct AssetPackageProgress
    {
        std::uint64_t CompletedBytes = 0;
        std::uint64_t TotalBytes = 0;
        std::filesystem::path CurrentFile;
    };

    struct AssetPackageCallbacks
    {
        std::function<bool()> Cancelled;
        std::function<void(const AssetPackageProgress&)> Progress;
    };

    using AssetPackageSignatureVerifier =
        std::function<bool(std::string_view algorithm, std::string_view keyId, std::span<const std::byte> message,
                           std::span<const std::byte> signature)>;

    struct AssetPackageVerification
    {
        bool RequireSignature = false;
        std::optional<std::uint64_t> ExpectedArchiveSizeBytes;
        std::string ExpectedArchiveSha256;
        AssetPackageSignatureVerifier VerifySignature;
    };

    struct AssetPackageArchiveMetadata
    {
        AssetPackageManifest Manifest;
        std::optional<AssetPackageSignature> Signature;
        std::vector<std::byte> ExactManifestBytes;
        std::uint64_t ArchiveSizeBytes = 0;
        std::string ArchiveSha256;
    };

    struct AssetPackageWriteRequest
    {
        AssetPackageManifest Manifest;
        std::filesystem::path PayloadRoot;
        std::filesystem::path Output;
        std::optional<AssetPackageSignature> Signature;
        int CompressionLevel = 9;
        AssetPackageCallbacks Callbacks;
    };

    struct AssetPackageExtractionRequest
    {
        std::filesystem::path Archive;
        std::filesystem::path AllowedStagingParent;
        std::filesystem::path StagingRoot;
        AssetPackageVerification Verification;
        AssetPackageCallbacks Callbacks;
    };

    struct AssetPackageExtractionResult
    {
        AssetPackageArchiveMetadata Metadata;
        std::filesystem::path StagingRoot;
    };

    KEIRE_API void ValidateAssetPackageManifest(const AssetPackageManifest& manifest);
    [[nodiscard]] KEIRE_API std::string EncodeAssetPackageManifest(const AssetPackageManifest& manifest);
    [[nodiscard]] KEIRE_API AssetPackageManifest DecodeAssetPackageManifest(std::string_view document);
    [[nodiscard]] KEIRE_API AssetPackageManifest InventoryAssetPackagePayload(AssetPackageManifest manifest,
                                                                              const std::filesystem::path& payloadRoot);
    [[nodiscard]] KEIRE_API AssetPackageArchiveMetadata
    WriteAssetPackageArchive(const AssetPackageWriteRequest& request);
    [[nodiscard]] KEIRE_API AssetPackageArchiveMetadata
    InspectAssetPackageArchive(const std::filesystem::path& archive, const AssetPackageVerification& verification = {});
    [[nodiscard]] KEIRE_API AssetPackageExtractionResult
    ExtractAssetPackageToStaging(const AssetPackageExtractionRequest& request);
} // namespace Keire
