#pragma once

#include "Keire/Assets/AssetPackage.h"
#include "Keire/Assets/AssetPipeline.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct AssetPackageSelection
    {
        std::vector<Keire::AssetId> Assets;
        std::optional<std::filesystem::path> Folder;
    };

    struct AssetPackageDraft
    {
        std::string PackageId;
        std::string Version = "0.0.1";
        std::string PublisherId = "local";
        std::string DisplayName;
        std::string Summary;
        std::string MinimumEngineVersion;
    };

    struct AssetPackageAuthoringRequest
    {
        std::filesystem::path ProjectRoot;
        std::filesystem::path SourceDirectory = "Assets";
        std::filesystem::path StagingParent;
        std::filesystem::path Output;
        AssetPackageSelection Selection;
        AssetPackageDraft Draft;
        std::vector<Keire::AssetSourceRecord> Records;
    };

    [[nodiscard]] std::string SuggestedAssetPackageIdentifier(std::string_view displayName);
    [[nodiscard]] std::vector<Keire::AssetSourceRecord>
    ResolveAssetPackageRecords(std::span<const Keire::AssetSourceRecord> records,
                               const AssetPackageSelection& selection);
    [[nodiscard]] Keire::AssetPackageArchiveMetadata
    CreateAssetPackageArchive(const AssetPackageAuthoringRequest& request);
} // namespace KeireEditor
