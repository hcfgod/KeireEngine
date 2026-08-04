#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Assets/AssetMetadata.h"
#include "Keire/Jobs/JobSystem.h"

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Keire
{
    namespace Detail
    {
        class AssetDatabaseWorkerAccess;
    }

    enum class AssetImportOptionKind : std::uint8_t
    {
        Boolean,
        Integer,
        Scalar,
        Choice
    };

    using AssetImportOptionValue = std::variant<bool, std::int64_t, double, std::string>;
    using AssetImportSettings = std::map<std::string, AssetImportOptionValue, std::less<>>;

    struct AssetImportOptionDescriptor
    {
        std::string Key;
        std::string DisplayName;
        std::string Group;
        AssetImportOptionKind Kind = AssetImportOptionKind::Boolean;
        AssetImportOptionValue DefaultValue = false;
        std::optional<double> Minimum;
        std::optional<double> Maximum;
        double Step = 1.0;
        std::vector<std::string> Choices;
    };

    enum class AssetTargetPlatform : std::uint8_t
    {
        Host,
        Windows,
        Linux,
        MacOS
    };

    struct AssetSourceDependency
    {
        std::filesystem::path RelativePath;
        std::string Digest;
    };

    enum class AssetDiagnosticSeverity : std::uint8_t
    {
        Information,
        Warning,
        Error
    };

    struct AssetImportDiagnostic
    {
        AssetDiagnosticSeverity Severity = AssetDiagnosticSeverity::Error;
        std::filesystem::path RelativePath;
        std::uint32_t Line = 0;
        std::uint32_t Column = 0;
        std::string Message;
    };

    struct AssetImportSource
    {
        AssetId Id;
        AssetTypeId Type;
        std::filesystem::path RelativePath;
    };

    struct AssetImportContext
    {
        AssetId Asset;
        std::filesystem::path ProjectRoot;
        std::filesystem::path SourceRoot;
        std::filesystem::path SourcePath;
        std::filesystem::path MetadataPath;
        std::filesystem::path RelativePath;
        std::size_t MaximumDependencyBytes = std::size_t{64} * 1024U * 1024U;
        std::function<std::vector<std::byte>(const std::filesystem::path&)> ReadProjectFile;
        AssetImportSettings ImportSettings;
        std::function<AssetId(std::string_view)> ResolveSubAssetId;
        std::function<AssetId(AssetId, std::string_view)> ResolveSubAssetIdFor;
        std::function<std::optional<AssetImportSource>(AssetId)> ResolveAssetSource;
    };

    struct AssetGeneratedSubAsset
    {
        AssetId Id;
        AssetTypeId Type;
        std::string Key;
        std::string Name;
        std::vector<std::byte> Bytes;
        std::vector<AssetId> AssetDependencies;
        AssetDerivedMetadata Metadata;
    };

    struct AssetImportOutput
    {
        std::vector<std::byte> Bytes;
        std::vector<AssetSourceDependency> SourceDependencies;
        std::vector<AssetImportDiagnostic> Diagnostics;
        std::vector<AssetId> AssetDependencies;
        AssetDerivedMetadata Metadata;
        std::vector<AssetGeneratedSubAsset> SubAssets;
        std::optional<AssetTypeId> PrimaryType;
    };

    struct AssetImporterRegistration
    {
        std::string Name;
        std::uint32_t Version = 1;
        AssetTypeId Type;
        std::vector<std::string> Extensions;
        std::function<std::vector<std::byte>(std::span<const std::byte>)> Import;
        std::function<AssetImportOutput(const AssetImportContext&, std::span<const std::byte>)> ContextualImport;
        std::function<std::vector<std::byte>(std::span<const std::byte>, AssetTargetPlatform)> Cook;
        std::vector<AssetImportOptionDescriptor> ImportOptions;
        std::function<AssetImportSettings(const AssetImportSettings&)> NormalizeImportSettings;
        std::function<AssetImportSettings(const std::filesystem::path&, const AssetImportSettings&)>
            SuggestImportSettings;
        std::function<AssetImportOutput(std::span<const std::byte>)> RestoreCachedOutput;
        std::vector<AssetTypeId> CompatibleTypes;
    };

    struct AssetSourceRecord
    {
        AssetId Id;
        AssetTypeId Type;
        std::filesystem::path RelativePath;
        std::filesystem::path MetadataPath;
        std::string Importer;
        std::uint32_t ImporterVersion = 1;
        std::string SourceDigest;
        std::string MetadataDigest;
        std::vector<AssetId> Dependencies;
        std::vector<AssetId> SubAssets;
        std::vector<AssetSourceDependency> SourceDependencies;
        AssetDerivedMetadata Metadata;
        AssetImportSettings ImportSettings;
    };

    struct AssetDatabaseSpecification
    {
        std::filesystem::path ProjectRoot = ".";
        std::filesystem::path SourceDirectory = "Assets";
        std::filesystem::path CacheDirectory = "Library/AssetCache";
        std::chrono::milliseconds ChangeDebounce = std::chrono::milliseconds(250);
        std::chrono::milliseconds ChangeMonitorInterval = std::chrono::milliseconds(100);
        std::size_t MaximumSourceBytes = std::size_t{1024} * 1024U * 1024U;
        std::vector<AssetImporterRegistration> Importers;
        Ref<JobSystem> Jobs;
    };

    struct AssetChangeMonitorStatistics
    {
        std::uint64_t PublishedScans = 0;
        std::uint64_t FailedScans = 0;
        bool ScanPending = false;
    };

    enum class AssetImportState : std::uint8_t
    {
        NotImported,
        Imported,
        CacheHit,
        Failed
    };

    struct AssetImportStatus
    {
        AssetId Id;
        AssetImportState State = AssetImportState::NotImported;
        std::vector<AssetImportDiagnostic> Diagnostics;
    };

    struct AssetImportResult
    {
        std::size_t Imported = 0;
        std::size_t CacheHits = 0;
        std::filesystem::path CatalogPath;
        std::vector<AssetImportStatus> Statuses;
    };

    enum class AssetImportPolicy : std::uint8_t
    {
        FailFast,
        KeepLastGood
    };

    enum class AssetOperationPhase : std::uint8_t
    {
        Scanning,
        Preflight,
        Staging,
        Importing,
        Cooking,
        Publishing,
        RollingBack,
        Completed
    };

    struct AssetOperationProgress
    {
        AssetOperationPhase Phase = AssetOperationPhase::Scanning;
        std::size_t Completed = 0;
        std::size_t Total = 0;
        std::filesystem::path CurrentPath;
    };

    using AssetOperationProgressCallback = std::function<void(const AssetOperationProgress&)>;

    class KEIRE_API AssetOperationCancelled final : public std::runtime_error
    {
      public:
        AssetOperationCancelled();
    };

    enum class ExternalAssetConflictPolicy : std::uint8_t
    {
        UniqueName,
        Replace,
        Skip
    };

    struct ExternalAssetImportItem
    {
        std::filesystem::path SourcePath;
        std::filesystem::path RelativeDestination;
        AssetImportSettings Settings;
        ExternalAssetConflictPolicy Conflict = ExternalAssetConflictPolicy::UniqueName;
    };

    struct ExternalAssetImportEntry
    {
        AssetId Id;
        std::filesystem::path SourcePath;
        std::filesystem::path RelativeDestination;
        bool Replaced = false;
    };

    class KEIRE_API ExternalAssetImportReceiptId final
    {
      public:
        constexpr ExternalAssetImportReceiptId() noexcept = default;

        [[nodiscard]] static ExternalAssetImportReceiptId Parse(std::string_view value);
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_Value); }
        [[nodiscard]] auto operator<=>(const ExternalAssetImportReceiptId&) const noexcept = default;

      private:
        friend class AssetDatabase;
        explicit ExternalAssetImportReceiptId(AssetId value) noexcept : m_Value(value) {}
        AssetId m_Value;
    };

    struct ExternalAssetImportResult
    {
        std::vector<ExternalAssetImportEntry> Entries;
        AssetImportResult Import;
        ExternalAssetImportReceiptId Receipt;
    };

    class KEIRE_API AssetTrashId final
    {
      public:
        constexpr AssetTrashId() noexcept = default;

        [[nodiscard]] static AssetTrashId Parse(std::string_view value);
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_Value); }
        [[nodiscard]] auto operator<=>(const AssetTrashId&) const noexcept = default;

      private:
        friend class AssetDatabase;
        explicit AssetTrashId(AssetId value) noexcept : m_Value(value) {}
        AssetId m_Value;
    };

    struct AssetTrashRecord
    {
        AssetTrashId Id;
        std::filesystem::path OriginalPath;
        std::filesystem::path TrashPath;
        std::vector<AssetId> Assets;
        bool Folder = false;
    };

    class KEIRE_API AssetDatabase final : public RefCounted
    {
      public:
        explicit AssetDatabase(AssetDatabaseSpecification specification = {});
        ~AssetDatabase() override;

        AssetDatabase(const AssetDatabase&) = delete;
        AssetDatabase& operator=(const AssetDatabase&) = delete;

        [[nodiscard]] std::size_t Refresh();
        [[nodiscard]] std::vector<AssetSourceRecord> Records() const;
        [[nodiscard]] std::optional<AssetSourceRecord> Find(AssetId id) const;
        [[nodiscard]] std::optional<AssetSourceRecord> Find(const std::filesystem::path& relativePath) const;
        [[nodiscard]] std::vector<AssetId> PollChangedAssets();
        [[nodiscard]] AssetChangeMonitorStatistics ChangeMonitorStatistics() const noexcept;
        [[nodiscard]] AssetImportResult ImportAll();
        [[nodiscard]] AssetImportResult ImportAll(AssetImportPolicy policy);
        [[nodiscard]] AssetImportResult ImportAll(AssetImportPolicy policy, std::stop_token cancellation,
                                                  AssetOperationProgressCallback progress = {});
        [[nodiscard]] AssetImportStatus ImportStatus(AssetId id) const;
        [[nodiscard]] std::optional<AssetImporterRegistration>
        FindImporterForPath(const std::filesystem::path& path) const;
        [[nodiscard]] ExternalAssetImportResult ImportExternal(std::span<const ExternalAssetImportItem> items,
                                                               std::stop_token cancellation = {},
                                                               AssetOperationProgressCallback progress = {});
        void UndoExternalImport(ExternalAssetImportReceiptId receipt);
        void RedoExternalImport(ExternalAssetImportReceiptId receipt);

        void CreateFolder(const std::filesystem::path& relativePath);
        [[nodiscard]] AssetId CreateAsset(const std::filesystem::path& relativePath,
                                          const AssetImporterRegistration& importer,
                                          std::span<const std::byte> sourceBytes,
                                          const AssetImportSettings& settings = {});
        void ReplaceAssetSource(AssetId id, std::span<const std::byte> sourceBytes);
        void SetImportSettings(AssetId id, const AssetImportSettings& settings);
        void RequestReimport(AssetId id);
        [[nodiscard]] AssetId ExtractMaterial(AssetId model, AssetId generatedMaterial,
                                              const std::filesystem::path& relativePath);
        [[nodiscard]] std::vector<AssetId> ExtractMaterials(AssetId model,
                                                            const std::filesystem::path& relativeDirectory);
        void Rename(AssetId id, std::string newName);
        void MoveAsset(AssetId id, const std::filesystem::path& destination);
        [[nodiscard]] AssetId Duplicate(AssetId id, const std::filesystem::path& destination);
        void MoveFolder(const std::filesystem::path& source, const std::filesystem::path& destination);
        [[nodiscard]] std::vector<AssetId> DuplicateFolder(const std::filesystem::path& source,
                                                           const std::filesystem::path& destination);
        [[nodiscard]] AssetTrashRecord TrashAsset(AssetId id);
        [[nodiscard]] AssetTrashRecord TrashFolder(const std::filesystem::path& relativePath);
        [[nodiscard]] std::vector<AssetTrashRecord> TrashRecords() const;
        void RestoreTrash(AssetTrashId id);
        void PermanentlyDeleteTrash(AssetTrashId id);
        [[nodiscard]] std::filesystem::path MoveToTrash(AssetId id);

        [[nodiscard]] const AssetDatabaseSpecification& Specification() const noexcept;

      private:
        friend class AssetCooker;
        friend class Detail::AssetDatabaseWorkerAccess;
        [[nodiscard]] std::size_t RefreshUnlocked();
        [[nodiscard]] AssetImportResult ImportAllUnlocked(AssetImportPolicy policy, std::stop_token cancellation,
                                                          AssetOperationProgressCallback progress);
        [[nodiscard]] AssetId CreateAssetUnlocked(const std::filesystem::path& relativePath,
                                                  const AssetImporterRegistration& importer,
                                                  std::span<const std::byte> sourceBytes,
                                                  const AssetImportSettings& settings);
        void MoveAssetUnlocked(AssetId id, const std::filesystem::path& destination);
        void ApplyExternalImportReceipt(ExternalAssetImportReceiptId receipt, bool applied);
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    struct AssetBuildProfile
    {
        std::string Name = "Development";
        AssetTargetPlatform Target = AssetTargetPlatform::Host;
        int CompressionLevel = 1;
        std::uint64_t MaximumPackBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        std::size_t StreamPageBytes = std::size_t{256} * 1024U;
        bool Strict = false;
        std::vector<AssetId> Roots;
        // Strict cooks use the catalog produced by the already-compiled runtime managed generation. Keeping the
        // payload opaque here avoids making the generic asset pipeline depend on scripting metadata types.
        bool ManagedTypeDiscoveryComplete = false;
        std::string ManagedTypeCatalog;
    };

    struct AssetCookResult
    {
        std::size_t AssetCount = 0;
        std::size_t PackCount = 0;
        std::uint64_t UncompressedBytes = 0;
        std::uint64_t CompressedBytes = 0;
        std::filesystem::path CatalogPath;
    };

    class KEIRE_API AssetCooker final
    {
      public:
        [[nodiscard]] static AssetCookResult Cook(const AssetDatabase& database, const AssetBuildProfile& profile,
                                                  const std::filesystem::path& outputDirectory,
                                                  std::stop_token cancellation = {},
                                                  AssetOperationProgressCallback progress = {});
        static void Validate(const std::filesystem::path& catalogPath,
                             std::size_t maximumAssetBytes = std::size_t{1024} * 1024U * 1024U);

      private:
        friend class AssetDatabase;
        [[nodiscard]] static AssetCookResult CookUnlocked(const AssetDatabase& database,
                                                          const AssetBuildProfile& profile,
                                                          const std::filesystem::path& outputDirectory,
                                                          std::stop_token cancellation,
                                                          AssetOperationProgressCallback progress);
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateTextAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateInputActionAssetImporter();
} // namespace Keire
