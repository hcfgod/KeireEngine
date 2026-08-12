#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPackage.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class ProjectAssetImportDisposition : std::uint8_t
    {
        Install,
        Replace,
        ReuseIdentical,
        KeepLocal,
        Conflict
    };

    enum class ProjectAssetImportResolution : std::uint8_t
    {
        Unresolved,
        Replace,
        KeepLocal
    };

    enum class ProjectAssetImportConflictKind : std::uint8_t
    {
        Path,
        AssetIdentity,
        ExecutableCodeConsent,
        MissingAssetDependency,
        ModifiedLocalFile,
        Compatibility
    };

    struct ProjectAssetImportDecision
    {
        std::filesystem::path Path;
        ProjectAssetImportResolution Resolution = ProjectAssetImportResolution::Unresolved;
    };

    struct ProjectAssetImportRequest
    {
        std::filesystem::path Archive;
        std::vector<AssetId> SelectedAssets;
        std::vector<ProjectAssetImportDecision> Decisions;
        std::optional<std::uint64_t> ExpectedArchiveSizeBytes;
        std::string ExpectedArchiveSha256;
        bool RequireMarketplaceSignature = true;
        bool AllowExecutableCode = false;
    };

    struct ProjectAssetImportPlanEntry
    {
        std::filesystem::path PackagePath;
        std::filesystem::path ProjectPath;
        std::string IncomingSha256;
        std::string LocalSha256;
        ProjectAssetImportDisposition Disposition = ProjectAssetImportDisposition::Install;
        bool RequiredDependency = false;
    };

    struct ProjectAssetImportConflict
    {
        ProjectAssetImportConflictKind Kind = ProjectAssetImportConflictKind::Path;
        std::filesystem::path Path;
        std::string Message;
    };

    struct ProjectAssetImportPlan
    {
        AssetPackageArchiveMetadata Package;
        std::vector<AssetId> ResolvedAssets;
        std::vector<ProjectAssetImportPlanEntry> Entries;
        std::vector<ProjectAssetImportConflict> Conflicts;
        bool ContainsExecutableCode = false;

        [[nodiscard]] bool Valid() const noexcept { return Conflicts.empty(); }
    };

    struct ProjectAssetImportReceiptEntry
    {
        std::filesystem::path ProjectPath;
        std::string PackageSha256;
        bool Owned = true;
    };

    struct ProjectAssetImportReceipt
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        std::uint32_t SchemaVersion = CurrentSchemaVersion;
        std::string PackageId;
        std::string Version;
        std::string ArchiveSha256;
        std::string ExecutableCodeFingerprint;
        bool ExecutableCodeApproved = false;
        std::vector<ProjectAssetImportReceiptEntry> Entries;
    };

    struct ProjectAssetImportResult
    {
        ProjectAssetImportReceipt Receipt;
        std::vector<std::filesystem::path> Written;
        std::vector<std::filesystem::path> Reused;
        std::vector<std::filesystem::path> Retained;
    };

    struct ProjectAssetRemovalResult
    {
        std::vector<std::filesystem::path> Removed;
        std::vector<std::filesystem::path> RetainedModified;
    };

    struct ProjectAssetImportRecoveryResult
    {
        std::size_t RecoveredOperations = 0;
        std::vector<std::string> Diagnostics;
    };

    enum class ProjectAssetImportState : std::uint8_t
    {
        Preflight,
        Extracting,
        Publishing,
        RollingBack,
        Recovering,
        Completed,
        Failed
    };

    struct ProjectAssetImportEvent
    {
        ProjectAssetImportState State = ProjectAssetImportState::Preflight;
        std::string OperationId;
        std::string PackageId;
        std::filesystem::path Path;
        std::uint64_t Completed = 0;
        std::uint64_t Total = 0;
        std::string Message;
    };

    using ProjectAssetImportEventCallback = std::function<void(const ProjectAssetImportEvent&)>;

    struct ProjectAssetPackageImporterSpecification
    {
        std::filesystem::path ProjectRoot;
        std::string EngineVersion;
        std::string Platform;
        std::string Architecture;
        std::vector<std::string> RendererCapabilities;
        AssetPackageSignatureVerifier VerifyMarketplaceSignature;
        ProjectAssetImportEventCallback Events;
    };

    [[nodiscard]] KEIRE_API std::string EncodeProjectAssetImportReceipt(const ProjectAssetImportReceipt& receipt);
    [[nodiscard]] KEIRE_API ProjectAssetImportReceipt DecodeProjectAssetImportReceipt(std::string_view document);

    class KEIRE_API ProjectAssetPackageImporter final
    {
      public:
        class Impl;

        explicit ProjectAssetPackageImporter(ProjectAssetPackageImporterSpecification specification);
        ~ProjectAssetPackageImporter();

        ProjectAssetPackageImporter(const ProjectAssetPackageImporter&) = delete;
        ProjectAssetPackageImporter& operator=(const ProjectAssetPackageImporter&) = delete;

        [[nodiscard]] ProjectAssetImportPlan Preflight(const ProjectAssetImportRequest& request) const;
        [[nodiscard]] ProjectAssetImportResult Import(const ProjectAssetImportRequest& request);
        [[nodiscard]] ProjectAssetRemovalResult Remove(std::string_view packageId);
        [[nodiscard]] ProjectAssetImportRecoveryResult RecoverInterruptedOperations();
        [[nodiscard]] std::optional<ProjectAssetImportReceipt> Receipt(std::string_view packageId) const;

        [[nodiscard]] static std::filesystem::path ReceiptPath(const std::filesystem::path& projectRoot,
                                                               std::string_view packageId);

      private:
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
