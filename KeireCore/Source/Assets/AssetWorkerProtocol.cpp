#include "KeireInternal/Assets/AssetWorkerProtocol.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        template <typename Enum>
        [[nodiscard]] Enum DecodeEnum(const Json& value, const std::uint8_t maximum, const char* name)
        {
            const auto encoded = value.get<std::uint8_t>();
            if (encoded > maximum)
                throw std::invalid_argument(std::string("Asset-worker ") + name + " is out of range.");
            return static_cast<Enum>(encoded);
        }

        [[nodiscard]] Json ReadJson(const std::filesystem::path& path)
        {
            constexpr std::uintmax_t maximumDocumentBytes = 64ULL * 1024ULL * 1024U;
            std::error_code error;
            if (std::filesystem::file_size(path, error) > maximumDocumentBytes || error)
                throw std::runtime_error("Asset-worker document is missing or exceeds the size limit: " +
                                         PathToUtf8(path));
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                throw std::runtime_error("Could not open asset-worker document: " + PathToUtf8(path));
            Json value;
            stream >> value;
            return value;
        }

        void WriteJson(const std::filesystem::path& path, const Json& value)
        {
            WriteTextFileAtomically(path, value.dump(2));
        }

        [[nodiscard]] AssetWorkerOperationKind ParseKind(const std::string_view value)
        {
            if (value == "import-all")
                return AssetWorkerOperationKind::ImportAll;
            if (value == "import-assets")
                return AssetWorkerOperationKind::ImportAssets;
            if (value == "external-import")
                return AssetWorkerOperationKind::ExternalImport;
            if (value == "create-asset")
                return AssetWorkerOperationKind::CreateAsset;
            if (value == "extract-materials")
                return AssetWorkerOperationKind::ExtractMaterials;
            if (value == "mutate")
                return AssetWorkerOperationKind::Mutate;
            if (value == "cook")
                return AssetWorkerOperationKind::Cook;
            if (value == "undo-external-import")
                return AssetWorkerOperationKind::UndoExternalImport;
            if (value == "redo-external-import")
                return AssetWorkerOperationKind::RedoExternalImport;
            if (value == "bake-lighting")
                return AssetWorkerOperationKind::BakeLighting;
            throw std::invalid_argument("Unknown asset-worker operation kind: " + std::string(value));
        }

        [[nodiscard]] Json EncodeSetting(const AssetImportOptionValue& value)
        {
            return std::visit([](const auto& item) { return Json(item); }, value);
        }

        [[nodiscard]] AssetImportOptionValue DecodeSetting(const Json& value)
        {
            if (value.is_boolean())
                return value.get<bool>();
            if (value.is_number_integer())
                return value.get<std::int64_t>();
            if (value.is_number_float())
                return value.get<double>();
            if (value.is_string())
                return value.get<std::string>();
            throw std::invalid_argument("Asset-worker import setting has an unsupported value type.");
        }

        [[nodiscard]] Json EncodeSettings(const AssetImportSettings& settings)
        {
            Json result = Json::object();
            for (const auto& [key, value] : settings)
                result[key] = EncodeSetting(value);
            return result;
        }

        [[nodiscard]] AssetImportSettings DecodeSettings(const Json& value)
        {
            if (!value.is_object())
                throw std::invalid_argument("Asset-worker import settings must be an object.");
            AssetImportSettings result;
            for (const auto& [key, setting] : value.items())
                result.emplace(key, DecodeSetting(setting));
            return result;
        }

        [[nodiscard]] Json EncodeRecord(const AssetSourceRecord& record)
        {
            Json dependencies = Json::array();
            for (const auto dependency : record.Dependencies)
                dependencies.push_back(dependency.ToString());
            Json subAssets = Json::array();
            for (const auto subAsset : record.SubAssets)
                subAssets.push_back(subAsset.ToString());
            Json sourceDependencies = Json::array();
            for (const auto& dependency : record.SourceDependencies)
                sourceDependencies.push_back(
                    {{"path", PathToUtf8(dependency.RelativePath)}, {"digest", dependency.Digest}});
            Json bounds;
            if (record.Metadata.LocalBounds)
                bounds = {{"minimum", record.Metadata.LocalBounds->Minimum},
                          {"maximum", record.Metadata.LocalBounds->Maximum}};
            return {{"id", record.Id.ToString()},
                    {"type", record.Type.ToString()},
                    {"relativePath", PathToUtf8(record.RelativePath)},
                    {"metadataPath", PathToUtf8(record.MetadataPath)},
                    {"importer", record.Importer},
                    {"importerVersion", record.ImporterVersion},
                    {"sourceDigest", record.SourceDigest},
                    {"metadataDigest", record.MetadataDigest},
                    {"dependencies", std::move(dependencies)},
                    {"subAssets", std::move(subAssets)},
                    {"sourceDependencies", std::move(sourceDependencies)},
                    {"bounds", std::move(bounds)},
                    {"settings", EncodeSettings(record.ImportSettings)}};
        }

        [[nodiscard]] AssetSourceRecord DecodeRecord(const Json& value)
        {
            AssetSourceRecord record;
            record.Id = AssetId::Parse(value.at("id").get<std::string>());
            record.Type = AssetTypeId::Parse(value.at("type").get<std::string>());
            record.RelativePath = PathFromUtf8(value.at("relativePath").get<std::string>());
            record.MetadataPath = PathFromUtf8(value.at("metadataPath").get<std::string>());
            record.Importer = value.at("importer").get<std::string>();
            record.ImporterVersion = value.at("importerVersion").get<std::uint32_t>();
            record.SourceDigest = value.at("sourceDigest").get<std::string>();
            record.MetadataDigest = value.at("metadataDigest").get<std::string>();
            for (const auto& dependency : value.value("dependencies", Json::array()))
                record.Dependencies.push_back(AssetId::Parse(dependency.get<std::string>()));
            for (const auto& subAsset : value.value("subAssets", Json::array()))
                record.SubAssets.push_back(AssetId::Parse(subAsset.get<std::string>()));
            for (const auto& dependency : value.value("sourceDependencies", Json::array()))
                record.SourceDependencies.push_back({PathFromUtf8(dependency.at("path").get<std::string>()),
                                                     dependency.at("digest").get<std::string>()});
            if (const auto& bounds = value.at("bounds"); !bounds.is_null())
                record.Metadata.LocalBounds = AssetBounds{bounds.at("minimum").get<std::array<float, 3>>(),
                                                          bounds.at("maximum").get<std::array<float, 3>>()};
            record.ImportSettings = DecodeSettings(value.value("settings", Json::object()));
            return record;
        }

        [[nodiscard]] Json EncodeImportResult(const AssetImportResult& result)
        {
            Json statuses = Json::array();
            for (const auto& status : result.Statuses)
            {
                Json diagnostics = Json::array();
                for (const auto& diagnostic : status.Diagnostics)
                    diagnostics.push_back({{"severity", static_cast<std::uint8_t>(diagnostic.Severity)},
                                           {"path", PathToUtf8(diagnostic.RelativePath)},
                                           {"line", diagnostic.Line},
                                           {"column", diagnostic.Column},
                                           {"message", diagnostic.Message}});
                statuses.push_back({{"id", status.Id.ToString()},
                                    {"state", static_cast<std::uint8_t>(status.State)},
                                    {"diagnostics", std::move(diagnostics)}});
            }
            return {{"imported", result.Imported},
                    {"cacheHits", result.CacheHits},
                    {"catalogPath", PathToUtf8(result.CatalogPath)},
                    {"statuses", std::move(statuses)}};
        }

        [[nodiscard]] AssetImportResult DecodeImportResult(const Json& value)
        {
            AssetImportResult result;
            result.Imported = value.value("imported", 0U);
            result.CacheHits = value.value("cacheHits", 0U);
            result.CatalogPath = PathFromUtf8(value.value("catalogPath", std::string{}));
            for (const auto& encoded : value.value("statuses", Json::array()))
            {
                AssetImportStatus status;
                status.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                status.State = DecodeEnum<AssetImportState>(encoded.at("state"), 3, "import state");
                for (const auto& item : encoded.value("diagnostics", Json::array()))
                    status.Diagnostics.push_back(
                        {DecodeEnum<AssetDiagnosticSeverity>(item.at("severity"), 2, "diagnostic severity"),
                         PathFromUtf8(item.value("path", std::string{})), item.value("line", 0U),
                         item.value("column", 0U), item.value("message", std::string{})});
                result.Statuses.push_back(std::move(status));
            }
            return result;
        }
    } // namespace

    std::string_view AssetWorkerOperationName(const AssetWorkerOperationKind kind) noexcept
    {
        switch (kind)
        {
        case AssetWorkerOperationKind::ImportAll:
            return "import-all";
        case AssetWorkerOperationKind::ImportAssets:
            return "import-assets";
        case AssetWorkerOperationKind::ExternalImport:
            return "external-import";
        case AssetWorkerOperationKind::CreateAsset:
            return "create-asset";
        case AssetWorkerOperationKind::ExtractMaterials:
            return "extract-materials";
        case AssetWorkerOperationKind::Mutate:
            return "mutate";
        case AssetWorkerOperationKind::Cook:
            return "cook";
        case AssetWorkerOperationKind::UndoExternalImport:
            return "undo-external-import";
        case AssetWorkerOperationKind::RedoExternalImport:
            return "redo-external-import";
        case AssetWorkerOperationKind::BakeLighting:
            return "bake-lighting";
        }
        return "unknown";
    }

    void WriteAssetWorkerRequest(const std::filesystem::path& path, const AssetWorkerRequest& request)
    {
        Json items = Json::array();
        for (const auto& item : request.ExternalItems)
        {
            Json settings = Json::object();
            for (const auto& [key, value] : item.Settings)
                settings[key] = EncodeSetting(value);
            items.push_back({{"source", PathToUtf8(item.SourcePath)},
                             {"destination", PathToUtf8(item.RelativeDestination)},
                             {"conflict", static_cast<std::uint8_t>(item.Conflict)},
                             {"settings", std::move(settings)}});
        }
        Json roots = Json::array();
        for (const auto root : request.BuildProfile.Roots)
            roots.push_back(root.ToString());
        Json importAssets = Json::array();
        for (const auto asset : request.ImportAssets)
            importAssets.push_back(asset.ToString());
        Json auxiliarySources = Json::array();
        for (const auto& auxiliary : request.CreateAuxiliarySources)
            auxiliarySources.push_back({{"relativePath", PathToUtf8(auxiliary.RelativePath)},
                                        {"payloadPath", PathToUtf8(auxiliary.PayloadPath)}});
        WriteJson(path, {{"schemaVersion", 1},
                         {"operationId", request.OperationId},
                         {"kind", std::string(AssetWorkerOperationName(request.Kind))},
                         {"projectRoot", PathToUtf8(request.ProjectRoot)},
                         {"sourceIndexPath", PathToUtf8(request.SourceIndexPath)},
                         {"reason", request.Reason},
                         {"importAssets", std::move(importAssets)},
                         {"externalItems", std::move(items)},
                         {"createRelativePath", PathToUtf8(request.CreateRelativePath)},
                         {"createPayloadPath", PathToUtf8(request.CreatePayloadPath)},
                         {"createSettings", EncodeSettings(request.CreateSettings)},
                         {"createAuxiliarySources", std::move(auxiliarySources)},
                         {"extractModel", request.ExtractModel ? request.ExtractModel.ToString() : std::string{}},
                         {"extractDirectory", PathToUtf8(request.ExtractDirectory)},
                         {"mutation",
                          {{"kind", static_cast<std::uint8_t>(request.Mutation.Kind)},
                           {"asset", request.Mutation.Asset ? request.Mutation.Asset.ToString() : std::string{}},
                           {"trash", request.Mutation.Trash ? request.Mutation.Trash.ToString() : std::string{}},
                           {"source", PathToUtf8(request.Mutation.Source)},
                           {"destination", PathToUtf8(request.Mutation.Destination)}}},
                         {"cookOutput", PathToUtf8(request.CookOutput)},
                         {"receipt", request.Receipt ? request.Receipt.ToString() : std::string{}},
                         {"bakeScene", request.BakeScene ? request.BakeScene.ToString() : std::string{}},
                         {"bakeForce", request.BakeForce},
                         {"profile",
                          {{"name", request.BuildProfile.Name},
                           {"target", static_cast<std::uint8_t>(request.BuildProfile.Target)},
                           {"compression", request.BuildProfile.CompressionLevel},
                           {"maximumPackBytes", request.BuildProfile.MaximumPackBytes},
                           {"strict", request.BuildProfile.Strict},
                           {"managedTypeDiscoveryComplete", request.BuildProfile.ManagedTypeDiscoveryComplete},
                           {"managedTypeCatalog", request.BuildProfile.ManagedTypeCatalog},
                           {"roots", std::move(roots)}}}});
    }

    AssetWorkerRequest ReadAssetWorkerRequest(const std::filesystem::path& path)
    {
        const auto value = ReadJson(path);
        if (value.value("schemaVersion", 0) != 1)
            throw std::invalid_argument("Asset-worker request schema is unsupported.");
        AssetWorkerRequest request;
        request.OperationId = value.at("operationId").get<std::string>();
        (void)AssetId::Parse(request.OperationId);
        request.Kind = ParseKind(value.at("kind").get<std::string>());
        request.ProjectRoot = PathFromUtf8(value.at("projectRoot").get<std::string>());
        request.SourceIndexPath = PathFromUtf8(value.at("sourceIndexPath").get<std::string>());
        request.Reason = value.value("reason", std::string{});
        for (const auto& asset : value.value("importAssets", Json::array()))
            request.ImportAssets.push_back(AssetId::Parse(asset.get<std::string>()));
        request.CookOutput = PathFromUtf8(value.value("cookOutput", std::string{}));
        request.CreateRelativePath = PathFromUtf8(value.value("createRelativePath", std::string{}));
        request.CreatePayloadPath = PathFromUtf8(value.value("createPayloadPath", std::string{}));
        request.CreateSettings = DecodeSettings(value.value("createSettings", Json::object()));
        if (const auto model = value.value("extractModel", std::string{}); !model.empty())
            request.ExtractModel = AssetId::Parse(model);
        request.ExtractDirectory = PathFromUtf8(value.value("extractDirectory", std::string{}));
        for (const auto& auxiliary : value.value("createAuxiliarySources", Json::array()))
            request.CreateAuxiliarySources.push_back({PathFromUtf8(auxiliary.at("relativePath").get<std::string>()),
                                                      PathFromUtf8(auxiliary.at("payloadPath").get<std::string>())});
        if (const auto mutation = value.find("mutation"); mutation != value.end())
        {
            request.Mutation.Kind =
                DecodeEnum<AssetWorkerMutationKind>(mutation->value("kind", Json(0)), 8, "mutation kind");
            if (const auto asset = mutation->value("asset", std::string{}); !asset.empty())
                request.Mutation.Asset = AssetId::Parse(asset);
            if (const auto trash = mutation->value("trash", std::string{}); !trash.empty())
                request.Mutation.Trash = AssetTrashId::Parse(trash);
            request.Mutation.Source = PathFromUtf8(mutation->value("source", std::string{}));
            request.Mutation.Destination = PathFromUtf8(mutation->value("destination", std::string{}));
        }
        if (const auto receipt = value.value("receipt", std::string{}); !receipt.empty())
            request.Receipt = ExternalAssetImportReceiptId::Parse(receipt);
        if (const auto scene = value.value("bakeScene", std::string{}); !scene.empty())
            request.BakeScene = AssetId::Parse(scene);
        request.BakeForce = value.value("bakeForce", false);
        for (const auto& encoded : value.value("externalItems", Json::array()))
        {
            ExternalAssetImportItem item;
            item.SourcePath = PathFromUtf8(encoded.at("source").get<std::string>());
            item.RelativeDestination = PathFromUtf8(encoded.at("destination").get<std::string>());
            item.Conflict = DecodeEnum<ExternalAssetConflictPolicy>(encoded.at("conflict"), 2, "conflict policy");
            item.Settings = DecodeSettings(encoded.value("settings", Json::object()));
            request.ExternalItems.push_back(std::move(item));
        }
        const auto& profile = value.at("profile");
        request.BuildProfile.Name = profile.value("name", std::string("Development"));
        request.BuildProfile.Target =
            DecodeEnum<AssetTargetPlatform>(profile.value("target", Json(0)), 3, "target platform");
        request.BuildProfile.CompressionLevel = profile.value("compression", 6);
        request.BuildProfile.MaximumPackBytes = profile.value("maximumPackBytes", 2ULL * 1024ULL * 1024ULL * 1024ULL);
        request.BuildProfile.Strict = profile.value("strict", false);
        request.BuildProfile.ManagedTypeDiscoveryComplete = profile.value("managedTypeDiscoveryComplete", false);
        request.BuildProfile.ManagedTypeCatalog = profile.value("managedTypeCatalog", std::string{});
        for (const auto& root : profile.value("roots", Json::array()))
            request.BuildProfile.Roots.push_back(AssetId::Parse(root.get<std::string>()));
        return request;
    }

    void WriteAssetWorkerProgress(const std::filesystem::path& path, const AssetOperationProgress& progress)
    {
        WriteJson(path, {{"schemaVersion", 1},
                         {"phase", static_cast<std::uint8_t>(progress.Phase)},
                         {"completed", progress.Completed},
                         {"total", progress.Total},
                         {"currentPath", PathToUtf8(progress.CurrentPath)}});
    }

    std::optional<AssetOperationProgress> ReadAssetWorkerProgress(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path))
            return std::nullopt;
        const auto value = ReadJson(path);
        if (value.value("schemaVersion", 0) != 1)
            return std::nullopt;
        return AssetOperationProgress{DecodeEnum<AssetOperationPhase>(value.at("phase"), 7, "operation phase"),
                                      value.value("completed", 0U), value.value("total", 0U),
                                      PathFromUtf8(value.value("currentPath", std::string{}))};
    }

    void WriteAssetWorkerResult(const std::filesystem::path& path, const AssetWorkerResult& result)
    {
        Json entries = Json::array();
        for (const auto& entry : result.ExternalEntries)
            entries.push_back({{"id", entry.Id.ToString()},
                               {"source", PathToUtf8(entry.SourcePath)},
                               {"destination", PathToUtf8(entry.RelativeDestination)},
                               {"replaced", entry.Replaced}});
        Json mutatedAssets = Json::array();
        for (const auto asset : result.MutatedAssets)
            mutatedAssets.push_back(asset.ToString());
        Json value{{"schemaVersion", 1},
                   {"success", result.Success},
                   {"cancelled", result.Cancelled},
                   {"diagnostic", result.Diagnostic},
                   {"createdAsset", result.CreatedAsset ? result.CreatedAsset.ToString() : std::string{}},
                   {"mutatedAssets", std::move(mutatedAssets)},
                   {"trash", result.Trash ? result.Trash.ToString() : std::string{}},
                   {"import", EncodeImportResult(result.Import)},
                   {"externalEntries", std::move(entries)},
                   {"receipt", result.Receipt ? result.Receipt.ToString() : std::string{}}};
        value["lightingCacheHit"] = result.LightingCacheHit;
        if (result.Cook)
            value["cook"] = {{"assetCount", result.Cook->AssetCount},
                             {"packCount", result.Cook->PackCount},
                             {"uncompressedBytes", result.Cook->UncompressedBytes},
                             {"compressedBytes", result.Cook->CompressedBytes},
                             {"catalogPath", PathToUtf8(result.Cook->CatalogPath)}};
        WriteJson(path, value);
    }

    AssetWorkerResult ReadAssetWorkerResult(const std::filesystem::path& path)
    {
        const auto value = ReadJson(path);
        if (value.value("schemaVersion", 0) != 1)
            throw std::invalid_argument("Asset-worker result schema is unsupported.");
        AssetWorkerResult result;
        result.Success = value.value("success", false);
        result.Cancelled = value.value("cancelled", false);
        result.Diagnostic = value.value("diagnostic", std::string{});
        result.LightingCacheHit = value.value("lightingCacheHit", false);
        if (const auto created = value.value("createdAsset", std::string{}); !created.empty())
            result.CreatedAsset = AssetId::Parse(created);
        for (const auto& asset : value.value("mutatedAssets", Json::array()))
            result.MutatedAssets.push_back(AssetId::Parse(asset.get<std::string>()));
        if (const auto trash = value.value("trash", std::string{}); !trash.empty())
            result.Trash = AssetTrashId::Parse(trash);
        result.Import = DecodeImportResult(value.at("import"));
        for (const auto& encoded : value.value("externalEntries", Json::array()))
            result.ExternalEntries.push_back({AssetId::Parse(encoded.at("id").get<std::string>()),
                                              PathFromUtf8(encoded.value("source", std::string{})),
                                              PathFromUtf8(encoded.value("destination", std::string{})),
                                              encoded.value("replaced", false)});
        if (const auto receipt = value.value("receipt", std::string{}); !receipt.empty())
            result.Receipt = ExternalAssetImportReceiptId::Parse(receipt);
        if (value.contains("cook"))
        {
            const auto& cook = value.at("cook");
            result.Cook = AssetCookResult{cook.value("assetCount", 0U), cook.value("packCount", 0U),
                                          cook.value("uncompressedBytes", 0ULL), cook.value("compressedBytes", 0ULL),
                                          PathFromUtf8(cook.value("catalogPath", std::string{}))};
        }
        return result;
    }

    void WriteAssetSourceIndex(const std::filesystem::path& path, const std::span<const AssetSourceRecord> records)
    {
        Json encoded = Json::array();
        for (const auto& record : records)
            encoded.push_back(EncodeRecord(record));
        WriteJson(path, {{"schemaVersion", 1}, {"records", std::move(encoded)}});
    }

    std::vector<AssetSourceRecord> ReadAssetSourceIndex(const std::filesystem::path& path)
    {
        const auto value = ReadJson(path);
        if (value.value("schemaVersion", 0) != 1 || !value.contains("records") || !value.at("records").is_array())
            throw std::invalid_argument("Published asset source index is invalid or unsupported.");
        std::vector<AssetSourceRecord> records;
        records.reserve(value.at("records").size());
        for (const auto& record : value.at("records"))
            records.push_back(DecodeRecord(record));
        return records;
    }
} // namespace Keire::Detail
