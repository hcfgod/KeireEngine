#include "Keire/Project/ShaderGraphMigration.h"

#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumMigrationSourceBytes = std::size_t{32} * 1024U * 1024U;

        struct PendingMigration
        {
            ShaderGraphMigrationItem Item;
            ShaderGraphDefinition ShaderDefinition;
            MaterialGraphDefinition MaterialDefinition;
            Json MaterialMetadata;
            Json ShaderMetadata;
        };

        struct TransactionEntry
        {
            std::filesystem::path Destination;
            std::filesystem::path Staged;
            std::filesystem::path Backup;
            bool HadDestination = false;
            bool BackupReady = false;
            bool Installed = false;
        };

        [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > MaximumMigrationSourceBytes)
                throw std::runtime_error("Migration source is missing or exceeds 32 MiB: " + path.string());
            std::vector<std::byte> result(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream || (!result.empty() && !stream.read(reinterpret_cast<char*>(result.data()),
                                                            static_cast<std::streamsize>(result.size()))))
                throw std::runtime_error("Could not read migration source: " + path.string());
            return result;
        }

        [[nodiscard]] Json ReadJson(const std::filesystem::path& path)
        {
            const auto bytes = ReadBytes(path);
            return Json::parse(reinterpret_cast<const char*>(bytes.data()),
                               reinterpret_cast<const char*>(bytes.data() + bytes.size()));
        }

        void WriteBytes(const std::filesystem::path& path, const std::span<const std::byte> bytes)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream ||
                (!bytes.empty() && !stream.write(reinterpret_cast<const char*>(bytes.data()),
                                                 static_cast<std::streamsize>(bytes.size()))) ||
                !stream.flush())
                throw std::runtime_error("Could not stage migration output: " + path.string());
        }

        void WriteJson(const std::filesystem::path& path, const Json& value)
        {
            const auto text = value.dump(2) + '\n';
            WriteBytes(path, std::as_bytes(std::span(text.data(), text.size())));
        }

        [[nodiscard]] std::filesystem::path MetadataPath(const std::filesystem::path& source)
        {
            auto result = source;
            result += ".keiremeta";
            return result;
        }

        [[nodiscard]] MaterialGraphDefinition CreateMaterialDefinition(const ShaderGraphDefinition& graph,
                                                                       const AssetId shaderGraph)
        {
            MaterialShaderReference shader;
            shader.Kind = MaterialShaderSourceKind::ShaderGraph;
            shader.Asset = shaderGraph;
            for (const auto& keyword : graph.Keywords)
            {
                const auto value = keyword.DefaultOption.empty()
                                       ? (keyword.Options.empty() ? std::string("false") : keyword.Options.front())
                                       : keyword.DefaultOption;
                shader.Keywords.emplace(keyword.Name, value);
            }
            ShaderInterfaceDefinition interfaceDefinition;
            for (const auto& node : graph.Nodes)
                if (node.Kind == ShaderGraphNodeKind::Parameter)
                {
                    ShaderPropertyDefinition property;
                    property.Id = node.Id;
                    property.Name = node.Symbol;
                    property.Type = static_cast<ShaderPropertyType>(node.ValueType);
                    property.DefaultTexture =
                        node.ValueType == ShaderGraphValueType::Texture2D ? std::get<AssetId>(node.Value) : AssetId{};
                    if (const auto* scalar = std::get_if<float>(&node.Value))
                        property.DefaultValue.X = *scalar;
                    else if (const auto* vector2 = std::get_if<Vector2>(&node.Value))
                        property.DefaultValue = {vector2->X, vector2->Y, 0.0F, 0.0F};
                    else if (const auto* vector3 = std::get_if<Vector3>(&node.Value))
                        property.DefaultValue = {vector3->X, vector3->Y, vector3->Z, 0.0F};
                    else if (const auto* vector4 = std::get_if<Vector4>(&node.Value))
                        property.DefaultValue = *vector4;
                    else if (const auto* color = std::get_if<Color>(&node.Value))
                        property.DefaultValue = {color->Red, color->Green, color->Blue, color->Alpha};
                    interfaceDefinition.Properties.push_back(std::move(property));
                }
            auto result = CreateMaterialGraph(std::move(shader), interfaceDefinition);
            if (graph.Output == ShaderGraphOutput::Transparent || graph.Output == ShaderGraphOutput::Decal)
                result.Surface.AlphaMode = MaterialAlphaMode::Blend;
            else if (graph.Output == ShaderGraphOutput::Hair)
                result.Surface.AlphaMode = MaterialAlphaMode::Mask;
            result.Surface.DoubleSided =
                graph.Output == ShaderGraphOutput::Decal || graph.Output == ShaderGraphOutput::Hair;
            ValidateMaterialGraph(result);
            return result;
        }

        [[nodiscard]] std::optional<PendingMigration> InspectSource(const std::filesystem::path& assets,
                                                                    const std::filesystem::path& source,
                                                                    ShaderGraphMigrationItem& item)
        {
            item.MaterialGraph = std::filesystem::relative(source, assets);
            item.ShaderGraph =
                item.MaterialGraph.parent_path() / (item.MaterialGraph.stem().string() + "_Shader.keireshadergraph");
            const auto bytes = ReadBytes(source);
            try
            {
                (void)MaterialGraphAsset::DecodeSource(bytes);
                item.Disposition = ShaderGraphMigrationDisposition::AlreadyMigrated;
                item.Diagnostic = "Material Graph already uses the shader-binding schema.";
                return std::nullopt;
            }
            catch (const std::exception&)
            {
            }

            PendingMigration pending;
            pending.Item = item;
            try
            {
                pending.ShaderDefinition = ShaderGraphAsset::DecodeSource(bytes);
                const auto shaderPath = assets / item.ShaderGraph;
                if (std::filesystem::exists(shaderPath) || std::filesystem::exists(MetadataPath(shaderPath)))
                {
                    item.Disposition = ShaderGraphMigrationDisposition::Conflict;
                    item.Diagnostic = "The destination Shader Graph or its metadata already exists.";
                    return std::nullopt;
                }
                const auto materialMetadataPath = MetadataPath(source);
                pending.MaterialMetadata = ReadJson(materialMetadataPath);
                const auto materialAsset = AssetId::Parse(pending.MaterialMetadata.at("id").get<std::string>());
                const auto shaderAsset = AssetId::Generate();
                pending.ShaderDefinition.GeneratedAssetOwner = materialAsset;
                pending.MaterialDefinition = CreateMaterialDefinition(pending.ShaderDefinition, shaderAsset);

                Json shaderSubAssets = Json::array();
                Json materialSubAssets = Json::array();
                const auto oldSubAssets = pending.MaterialMetadata.value("subAssets", Json::array());
                if (!oldSubAssets.is_array())
                    throw std::invalid_argument("Legacy Material Graph metadata subassets must be an array.");
                for (std::size_t index = 0; index < oldSubAssets.size(); ++index)
                {
                    if (index + 1U == oldSubAssets.size())
                        materialSubAssets.push_back(oldSubAssets[index]);
                    else
                        shaderSubAssets.push_back(oldSubAssets[index]);
                }
                pending.ShaderMetadata = {{"schemaVersion", 1},
                                          {"id", shaderAsset.ToString()},
                                          {"type", ShaderGraphAsset::StaticType().Value().ToString()},
                                          {"importer", "Keire.ShaderGraph"},
                                          {"importerVersion", 14},
                                          {"dependencies", Json::array()},
                                          {"subAssets", std::move(shaderSubAssets)}};
                pending.MaterialMetadata["type"] = MaterialGraphAsset::StaticType().Value().ToString();
                pending.MaterialMetadata["importer"] = "Keire.MaterialGraph";
                pending.MaterialMetadata["importerVersion"] = 1;
                pending.MaterialMetadata["dependencies"] = Json::array({shaderAsset.ToString()});
                pending.MaterialMetadata["subAssets"] = std::move(materialSubAssets);
                item.Disposition = ShaderGraphMigrationDisposition::Migrate;
                item.Diagnostic = "Procedural nodes will move to a Shader Graph; material defaults remain here.";
                pending.Item = item;
                return pending;
            }
            catch (const std::exception& error)
            {
                item.Disposition = ShaderGraphMigrationDisposition::Invalid;
                item.Diagnostic = error.what();
                return std::nullopt;
            }
        }

        [[nodiscard]] std::pair<ShaderGraphMigrationReport, std::vector<PendingMigration>>
        Inspect(const std::filesystem::path& projectRoot)
        {
            const auto root = std::filesystem::absolute(projectRoot).lexically_normal();
            const auto assets = root / "Assets";
            std::error_code error;
            if (!std::filesystem::is_directory(assets, error) || error)
                throw std::invalid_argument("Shader Graph migration requires a project with an Assets directory.");

            ShaderGraphMigrationReport report;
            std::vector<PendingMigration> pending;
            for (std::filesystem::recursive_directory_iterator
                     iterator(assets, std::filesystem::directory_options::skip_permission_denied, error),
                 end;
                 iterator != end; iterator.increment(error))
            {
                if (error)
                    throw std::runtime_error("Could not enumerate project assets for Shader Graph migration.");
                if (!iterator->is_regular_file(error) || error ||
                    iterator->path().extension().string() != MaterialAssetSourceExtension)
                {
                    error.clear();
                    continue;
                }
                ShaderGraphMigrationItem item;
                if (auto candidate = InspectSource(assets, iterator->path(), item))
                    pending.push_back(std::move(*candidate));
                report.Items.push_back(std::move(item));
            }
            std::ranges::sort(report.Items, {}, &ShaderGraphMigrationItem::MaterialGraph);
            std::ranges::sort(pending, {}, [](const PendingMigration& value) { return value.Item.MaterialGraph; });
            return {std::move(report), std::move(pending)};
        }

        void CommitTransaction(std::vector<TransactionEntry>& entries)
        {
            try
            {
                for (auto& entry : entries)
                {
                    entry.HadDestination = std::filesystem::exists(entry.Destination);
                    if (entry.HadDestination)
                    {
                        std::filesystem::rename(entry.Destination, entry.Backup);
                        entry.BackupReady = true;
                    }
                    std::filesystem::create_directories(entry.Destination.parent_path());
                    std::filesystem::rename(entry.Staged, entry.Destination);
                    entry.Installed = true;
                }
            }
            catch (...)
            {
                for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
                {
                    std::error_code ignored;
                    if (entry->Installed)
                        std::filesystem::remove(entry->Destination, ignored);
                    if (entry->BackupReady)
                        std::filesystem::rename(entry->Backup, entry->Destination, ignored);
                }
                throw;
            }
        }
    } // namespace

    std::size_t ShaderGraphMigrationReport::PendingCount() const noexcept
    {
        return static_cast<std::size_t>(std::ranges::count(Items, ShaderGraphMigrationDisposition::Migrate,
                                                           &ShaderGraphMigrationItem::Disposition));
    }

    bool ShaderGraphMigrationReport::CanApply() const noexcept
    {
        return std::ranges::none_of(Items,
                                    [](const ShaderGraphMigrationItem& item)
                                    {
                                        return item.Disposition == ShaderGraphMigrationDisposition::Conflict ||
                                               item.Disposition == ShaderGraphMigrationDisposition::Invalid;
                                    });
    }

    ShaderGraphMigrationReport InspectShaderGraphMigration(const std::filesystem::path& projectRoot)
    {
        return Inspect(projectRoot).first;
    }

    ShaderGraphMigrationReport ApplyShaderGraphMigration(const std::filesystem::path& projectRoot)
    {
        auto [report, pending] = Inspect(projectRoot);
        if (!report.CanApply())
            throw std::runtime_error(
                "Shader Graph migration has conflicts or invalid legacy assets; no files changed.");
        if (pending.empty())
            return report;

        const auto root = std::filesystem::absolute(projectRoot).lexically_normal();
        const auto assets = root / "Assets";
        const auto transaction = root / "Library" / "ShaderGraphMigration" / AssetId::Generate().ToString();
        std::filesystem::create_directories(transaction / "staged");
        std::filesystem::create_directories(transaction / "backup");
        std::vector<TransactionEntry> entries;
        try
        {
            for (std::size_t index = 0; index < pending.size(); ++index)
            {
                const auto& migration = pending[index];
                const auto prefix = std::to_string(index);
                const auto materialSource = assets / migration.Item.MaterialGraph;
                const auto shaderSource = assets / migration.Item.ShaderGraph;
                const auto stagedMaterial = transaction / "staged" / (prefix + ".material");
                const auto stagedMaterialMetadata = transaction / "staged" / (prefix + ".material.meta");
                const auto stagedShader = transaction / "staged" / (prefix + ".shader");
                const auto stagedShaderMetadata = transaction / "staged" / (prefix + ".shader.meta");
                WriteBytes(stagedMaterial, MaterialGraphAsset::EncodeSource(migration.MaterialDefinition));
                WriteJson(stagedMaterialMetadata, migration.MaterialMetadata);
                WriteBytes(stagedShader, ShaderGraphAsset::EncodeSource(migration.ShaderDefinition));
                WriteJson(stagedShaderMetadata, migration.ShaderMetadata);
                entries.push_back({materialSource, stagedMaterial, transaction / "backup" / (prefix + ".material")});
                entries.push_back({MetadataPath(materialSource), stagedMaterialMetadata,
                                   transaction / "backup" / (prefix + ".material.meta")});
                entries.push_back({shaderSource, stagedShader, transaction / "backup" / (prefix + ".shader")});
                entries.push_back({MetadataPath(shaderSource), stagedShaderMetadata,
                                   transaction / "backup" / (prefix + ".shader.meta")});
            }
            CommitTransaction(entries);
            std::error_code ignored;
            std::filesystem::remove_all(transaction, ignored);
            return report;
        }
        catch (...)
        {
            // A failed transaction intentionally retains its staging and backup directory for manual recovery.
            // CommitTransaction has already attempted a reverse-order rollback without masking the original failure.
            throw;
        }
    }
} // namespace Keire
