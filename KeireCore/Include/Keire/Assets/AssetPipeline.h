#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Assets/AssetMetadata.h"

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
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

    struct AssetImportContext
    {
        std::filesystem::path ProjectRoot;
        std::filesystem::path SourceRoot;
        std::filesystem::path SourcePath;
        std::filesystem::path MetadataPath;
        std::filesystem::path RelativePath;
        std::size_t MaximumDependencyBytes = 64U * 1024U * 1024U;
        std::function<std::vector<std::byte>(const std::filesystem::path&)> ReadProjectFile;
    };

    struct AssetImportOutput
    {
        std::vector<std::byte> Bytes;
        std::vector<AssetSourceDependency> SourceDependencies;
        std::vector<AssetImportDiagnostic> Diagnostics;
        std::vector<AssetId> AssetDependencies;
        AssetDerivedMetadata Metadata;
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
    };

    struct AssetDatabaseSpecification
    {
        std::filesystem::path ProjectRoot = ".";
        std::filesystem::path SourceDirectory = "Assets";
        std::filesystem::path CacheDirectory = "Library/AssetCache";
        std::chrono::milliseconds ChangeDebounce = std::chrono::milliseconds(250);
        std::size_t MaximumSourceBytes = 1024U * 1024U * 1024U;
        std::vector<AssetImporterRegistration> Importers;
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

    class KEIRE_API AssetTrashId final
    {
      public:
        constexpr AssetTrashId() noexcept = default;

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
        [[nodiscard]] AssetImportResult ImportAll();
        [[nodiscard]] AssetImportResult ImportAll(AssetImportPolicy policy);
        [[nodiscard]] AssetImportStatus ImportStatus(AssetId id) const;

        void CreateFolder(const std::filesystem::path& relativePath);
        [[nodiscard]] AssetId CreateAsset(const std::filesystem::path& relativePath,
                                          const AssetImporterRegistration& importer,
                                          std::span<const std::byte> sourceBytes);
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
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    struct AssetBuildProfile
    {
        std::string Name = "Development";
        AssetTargetPlatform Target = AssetTargetPlatform::Host;
        int CompressionLevel = 6;
        std::uint64_t MaximumPackBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        bool Strict = false;
        std::vector<AssetId> Roots;
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
                                                  const std::filesystem::path& outputDirectory);
        static void Validate(const std::filesystem::path& catalogPath,
                             std::size_t maximumAssetBytes = 1024U * 1024U * 1024U);
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateInputActionAssetImporter();
} // namespace Keire
