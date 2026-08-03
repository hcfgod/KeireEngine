#pragma once

#include "Keire/Assets/AssetPipeline.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Keire::Detail
{
    enum class AssetWorkerOperationKind : std::uint8_t
    {
        ImportAll,
        ExternalImport,
        CreateAsset,
        ExtractMaterials,
        Mutate,
        Cook,
        UndoExternalImport,
        RedoExternalImport,
        BakeLighting
    };

    enum class AssetWorkerMutationKind : std::uint8_t
    {
        CreateFolder,
        MoveAsset,
        DuplicateAsset,
        MoveFolder,
        DuplicateFolder,
        TrashAsset,
        TrashFolder,
        RestoreTrash,
        PermanentlyDeleteTrash
    };

    struct AssetWorkerMutation
    {
        AssetWorkerMutationKind Kind = AssetWorkerMutationKind::CreateFolder;
        AssetId Asset;
        AssetTrashId Trash;
        std::filesystem::path Source;
        std::filesystem::path Destination;
    };

    struct AssetWorkerAuxiliarySource
    {
        std::filesystem::path RelativePath;
        std::filesystem::path PayloadPath;
    };

    struct AssetWorkerRequest
    {
        std::string OperationId;
        AssetWorkerOperationKind Kind = AssetWorkerOperationKind::ImportAll;
        std::filesystem::path ProjectRoot;
        std::filesystem::path SourceIndexPath;
        std::vector<ExternalAssetImportItem> ExternalItems;
        std::filesystem::path CreateRelativePath;
        std::filesystem::path CreatePayloadPath;
        AssetImportSettings CreateSettings;
        std::vector<AssetWorkerAuxiliarySource> CreateAuxiliarySources;
        AssetId ExtractModel;
        std::filesystem::path ExtractDirectory;
        AssetWorkerMutation Mutation;
        AssetBuildProfile BuildProfile;
        std::filesystem::path CookOutput;
        ExternalAssetImportReceiptId Receipt;
        AssetId BakeScene;
        bool BakeForce = false;
    };

    struct AssetWorkerResult
    {
        bool Success = false;
        bool Cancelled = false;
        std::string Diagnostic;
        AssetImportResult Import;
        std::vector<ExternalAssetImportEntry> ExternalEntries;
        ExternalAssetImportReceiptId Receipt;
        AssetId CreatedAsset;
        std::vector<AssetId> MutatedAssets;
        AssetTrashId Trash;
        std::optional<AssetCookResult> Cook;
        bool LightingCacheHit = false;
    };

    void WriteAssetWorkerRequest(const std::filesystem::path& path, const AssetWorkerRequest& request);
    [[nodiscard]] AssetWorkerRequest ReadAssetWorkerRequest(const std::filesystem::path& path);
    void WriteAssetWorkerProgress(const std::filesystem::path& path, const AssetOperationProgress& progress);
    [[nodiscard]] std::optional<AssetOperationProgress> ReadAssetWorkerProgress(const std::filesystem::path& path);
    void WriteAssetWorkerResult(const std::filesystem::path& path, const AssetWorkerResult& result);
    [[nodiscard]] AssetWorkerResult ReadAssetWorkerResult(const std::filesystem::path& path);

    void WriteAssetSourceIndex(const std::filesystem::path& path, std::span<const AssetSourceRecord> records);
    [[nodiscard]] std::vector<AssetSourceRecord> ReadAssetSourceIndex(const std::filesystem::path& path);
} // namespace Keire::Detail
