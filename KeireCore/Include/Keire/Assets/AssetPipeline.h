#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"

#include <chrono>
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
    struct AssetImporterRegistration
    {
        std::string Name;
        std::uint32_t Version = 1;
        AssetTypeId Type;
        std::vector<std::string> Extensions;
        std::function<std::vector<std::byte>(std::span<const std::byte>)> Import;
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
        std::vector<AssetId> Dependencies;
        std::vector<AssetId> SubAssets;
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

    struct AssetImportResult
    {
        std::size_t Imported = 0;
        std::size_t CacheHits = 0;
        std::filesystem::path CatalogPath;
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

        void CreateFolder(const std::filesystem::path& relativePath);
        [[nodiscard]] AssetId CreateAsset(const std::filesystem::path& relativePath,
                                          const AssetImporterRegistration& importer,
                                          std::span<const std::byte> sourceBytes);
        void Rename(AssetId id, std::string newName);
        [[nodiscard]] AssetId Duplicate(AssetId id, const std::filesystem::path& destination);
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
        int CompressionLevel = 6;
        std::uint64_t MaximumPackBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        bool Strict = false;
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
