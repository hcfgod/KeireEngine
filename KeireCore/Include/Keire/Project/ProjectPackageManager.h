#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPackage.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    struct ProjectPackageRequirement
    {
        std::string PackageId;
        std::string VersionRange;

        [[nodiscard]] bool operator==(const ProjectPackageRequirement&) const = default;
    };

    struct ProjectPackageManifest
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        std::vector<ProjectPackageRequirement> Dependencies;

        [[nodiscard]] bool operator==(const ProjectPackageManifest&) const = default;
    };

    struct ProjectPackageLockEntry
    {
        std::string PackageId;
        std::string Version;
        std::string ArchiveSha256;
        std::uint64_t ArchiveSizeBytes = 0;
        std::string Source;
        std::string SignatureKeyId;
        AssetPackageInstallKind InstallKind = AssetPackageInstallKind::Registry;
        std::vector<AssetPackageDependency> Dependencies;
        bool Embedded = false;

        [[nodiscard]] bool operator==(const ProjectPackageLockEntry&) const = default;
    };

    struct ProjectPackageLock
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        std::vector<ProjectPackageLockEntry> Packages;

        [[nodiscard]] bool operator==(const ProjectPackageLock&) const = default;
    };

    enum class ProjectPackageState : std::uint8_t
    {
        Idle,
        Resolving,
        Verifying,
        Staging,
        Publishing,
        RollingBack,
        Recovering,
        Completed,
        Failed
    };

    enum class ProjectPackageTrust : std::uint8_t
    {
        Unverified,
        CatalogHashVerified,
        MarketplaceSignatureVerified,
        Embedded
    };

    enum class ProjectPackageConflictKind : std::uint8_t
    {
        MissingDependency,
        VersionMismatch,
        PackageConflict,
        EngineIncompatible,
        PlatformIncompatible,
        RendererIncompatible,
        EmbeddedPackage,
        AssetIdentity
    };

    struct ProjectPackageConflict
    {
        ProjectPackageConflictKind Kind = ProjectPackageConflictKind::MissingDependency;
        std::string PackageId;
        std::string RelatedPackageId;
        std::string Message;
    };

    struct ProjectPackageEvent
    {
        ProjectPackageState State = ProjectPackageState::Idle;
        std::string OperationId;
        std::string PackageId;
        std::uint64_t CompletedBytes = 0;
        std::uint64_t TotalBytes = 0;
        std::string Message;
    };

    using ProjectPackageEventCallback = std::function<void(const ProjectPackageEvent&)>;

    struct ProjectPackageManagerSpecification
    {
        std::filesystem::path ProjectRoot;
        std::filesystem::path GlobalCacheRoot;
        std::string EngineVersion;
        std::string Platform;
        std::string Architecture;
        std::vector<std::string> RendererCapabilities;
        AssetPackageSignatureVerifier VerifyMarketplaceSignature;
        ProjectPackageEventCallback Events;
    };

    struct ProjectPackageArchiveSource
    {
        std::filesystem::path Archive;
        std::string CatalogSource;
        std::uint64_t ExpectedArchiveSizeBytes = 0;
        std::string ExpectedArchiveSha256;
        bool RequireMarketplaceSignature = true;
    };

    struct ProjectPackageInstallRequest
    {
        std::vector<ProjectPackageArchiveSource> Archives;
        std::vector<ProjectPackageRequirement> DirectDependencies;
    };

    struct ProjectPackageInstallPlan
    {
        ProjectPackageManifest Manifest;
        ProjectPackageLock Lock;
        std::vector<AssetPackageArchiveMetadata> Archives;
        std::vector<ProjectPackageConflict> Conflicts;

        [[nodiscard]] bool Valid() const noexcept { return Conflicts.empty(); }
    };

    struct ProjectPackageMount
    {
        std::string PackageId;
        std::string Version;
        std::filesystem::path Root;
        ProjectPackageTrust Trust = ProjectPackageTrust::Unverified;
        bool ReadOnly = true;
    };

    struct ProjectPackageRecoveryResult
    {
        std::size_t RecoveredOperations = 0;
        std::vector<std::string> Diagnostics;
    };

    [[nodiscard]] KEIRE_API std::string EncodeProjectPackageManifest(const ProjectPackageManifest& manifest);
    [[nodiscard]] KEIRE_API ProjectPackageManifest DecodeProjectPackageManifest(std::string_view document);
    [[nodiscard]] KEIRE_API std::string EncodeProjectPackageLock(const ProjectPackageLock& lock);
    [[nodiscard]] KEIRE_API ProjectPackageLock DecodeProjectPackageLock(std::string_view document);
    [[nodiscard]] KEIRE_API bool AssetPackageVersionSatisfies(std::string_view version, std::string_view range);

    class KEIRE_API ProjectPackageManager final
    {
      public:
        class Impl;

        explicit ProjectPackageManager(ProjectPackageManagerSpecification specification);
        ~ProjectPackageManager();

        ProjectPackageManager(const ProjectPackageManager&) = delete;
        ProjectPackageManager& operator=(const ProjectPackageManager&) = delete;

        [[nodiscard]] ProjectPackageManifest Manifest() const;
        [[nodiscard]] ProjectPackageLock Lock() const;
        [[nodiscard]] ProjectPackageInstallPlan PreflightInstall(const ProjectPackageInstallRequest& request) const;
        [[nodiscard]] ProjectPackageLock Install(const ProjectPackageInstallRequest& request);
        [[nodiscard]] ProjectPackageLock Remove(std::string_view packageId);
        [[nodiscard]] ProjectPackageLock Embed(std::string_view packageId);
        [[nodiscard]] ProjectPackageLock RevertEmbedded(std::string_view packageId);
        [[nodiscard]] std::vector<ProjectPackageMount> Mounts() const;
        [[nodiscard]] ProjectPackageRecoveryResult RecoverInterruptedOperations();

        [[nodiscard]] static std::filesystem::path ManifestPath(const std::filesystem::path& projectRoot);
        [[nodiscard]] static std::filesystem::path LockPath(const std::filesystem::path& projectRoot);

      private:
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
