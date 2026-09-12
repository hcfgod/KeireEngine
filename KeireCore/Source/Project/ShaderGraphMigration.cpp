#include "Keire/Project/ShaderGraphMigration.h"

#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/ProjectFileTransaction.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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
            MaterialAuthoringDefinition MaterialDefinition;
            std::vector<std::byte> OriginalSource;
            std::vector<std::byte> OriginalMetadata;
            Json MaterialMetadata;
            Json ShaderMetadata;
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

        [[nodiscard]] bool HasMaterialSurfaceExpressions(const MaterialGraphDefinition& definition)
        {
            const auto master =
                std::ranges::find(definition.SurfaceGraph.Nodes, ShaderGraphNodeKind::Master, &ShaderGraphNode::Kind);
            return master != definition.SurfaceGraph.Nodes.end() &&
                   std::ranges::any_of(definition.SurfaceGraph.Connections, [&](const ShaderGraphConnection& connection)
                                       { return connection.Input.Node == master->Id; });
        }

        [[nodiscard]] MaterialAuthoringDefinition PropertyMaterial(const MaterialGraphDefinition& graph)
        {
            MaterialAuthoringDefinition result;
            result.Shader = graph.Shader;
            result.Surface = graph.Surface;
            result.ContributeEmissionToGI = graph.ContributeEmissionToGI;
            result.EmissiveGIIntensity = graph.EmissiveGIIntensity;
            const auto values = EvaluateMaterialGraphProperties(graph);
            for (const auto& property : graph.Properties)
            {
                if (property.Property)
                {
                    result.SchemaVersion = 5;
                    result.PropertyOverrides.push_back({property.Property, property.Name, values.at(property.Name)});
                }
                else
                    result.Properties.emplace(property.Name, values.at(property.Name));
            }
            return result;
        }

        [[nodiscard]] AssetId ExtractedShaderIdentity(const AssetId material)
        {
            const auto seed = material.ToString() + "/material-shader-upgrade/1";
            const auto digest = Detail::Sha256(std::as_bytes(std::span(seed.data(), seed.size())));
            std::uint64_t high = 0;
            std::uint64_t low = 0;
            for (std::size_t index = 0; index < 8; ++index)
            {
                high = (high << 8U) | std::to_integer<std::uint8_t>(digest[index]);
                low = (low << 8U) | std::to_integer<std::uint8_t>(digest[index + 8]);
            }
            return {high, low};
        }

        [[nodiscard]] ShaderGraphDefinition FindShaderGraph(const std::filesystem::path& assets, const AssetId id)
        {
            std::optional<ShaderGraphDefinition> result;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assets))
            {
                if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != ".keireshadergraph")
                    continue;
                if (!std::filesystem::exists(MetadataPath(entry.path())))
                    continue;
                const auto metadata = ReadJson(MetadataPath(entry.path()));
                if (AssetId::Parse(metadata.at("id").get<std::string>()) != id)
                    continue;
                if (result)
                    throw std::runtime_error("The Shader Graph identity is duplicated in the project.");
                result = ShaderGraphAsset::DecodeSource(ReadBytes(entry.path()));
            }
            if (!result)
                throw std::runtime_error("The material's Shader Graph dependency is unavailable for exact extraction.");
            return std::move(*result);
        }

        [[nodiscard]] std::optional<PendingMigration> InspectSource(const std::filesystem::path& assets,
                                                                    const std::filesystem::path& source,
                                                                    ShaderGraphMigrationItem& item)
        {
            item.MaterialGraph = std::filesystem::relative(source, assets);
            item.ShaderGraph =
                item.MaterialGraph.parent_path() / (item.MaterialGraph.stem().string() + "_Shader.keireshadergraph");
            PendingMigration pending;
            try
            {
                pending.OriginalSource = ReadBytes(source);
                const auto document = Json::parse(
                    reinterpret_cast<const char*>(pending.OriginalSource.data()),
                    reinterpret_cast<const char*>(pending.OriginalSource.data() + pending.OriginalSource.size()));
                if (document.value("kind", std::string{}) == "material")
                {
                    (void)MaterialAsset::DecodeAuthoringSource(pending.OriginalSource);
                    item.Disposition = ShaderGraphMigrationDisposition::AlreadyMigrated;
                    item.Diagnostic = "Material already uses property-only authoring.";
                    return std::nullopt;
                }
                pending.OriginalMetadata = ReadBytes(MetadataPath(source));
                pending.MaterialMetadata = ReadJson(MetadataPath(source));
                item.SourceAsset = AssetId::Parse(pending.MaterialMetadata.at("id").get<std::string>());
                std::optional<MaterialGraphDefinition> material;
                try
                {
                    material = MaterialGraphAsset::DecodeSource(pending.OriginalSource);
                }
                catch (const std::exception&)
                {
                }
                if (material)
                {
                    pending.MaterialDefinition = PropertyMaterial(*material);
                    if (!material->Shader.Asset)
                    {
                        pending.ShaderDefinition = material->SurfaceGraph;
                        item.CreatesShader = true;
                    }
                    else if (HasMaterialSurfaceExpressions(*material))
                    {
                        if (material->Shader.Kind != MaterialShaderSourceKind::ShaderGraph)
                            throw std::runtime_error(
                                "Executable code-shader material bindings cannot be extracted exactly.");
                        pending.ShaderDefinition =
                            ComposeMaterialGraphShader(*material, FindShaderGraph(assets, material->Shader.Asset));
                        item.CreatesShader = true;
                    }
                }
                else
                {
                    pending.ShaderDefinition = ShaderGraphAsset::DecodeSource(pending.OriginalSource);
                    item.CreatesShader = true;
                }
                if (item.CreatesShader)
                {
                    const auto shaderPath = assets / item.ShaderGraph;
                    if (std::filesystem::exists(shaderPath) || std::filesystem::exists(MetadataPath(shaderPath)))
                    {
                        item.Disposition = ShaderGraphMigrationDisposition::Conflict;
                        item.Diagnostic = "The destination Shader Graph or its metadata already exists.";
                        return std::nullopt;
                    }
                    item.GeneratedShaderAsset = ExtractedShaderIdentity(item.SourceAsset);
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(assets))
                    {
                        if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != ".keiremeta")
                            continue;
                        const auto metadata = ReadJson(entry.path());
                        if (AssetId::Parse(metadata.at("id").get<std::string>()) == item.GeneratedShaderAsset)
                        {
                            item.Disposition = ShaderGraphMigrationDisposition::Conflict;
                            item.Diagnostic = "The generated Shader Graph identity already belongs to another asset.";
                            return std::nullopt;
                        }
                    }
                    if (pending.ShaderDefinition.Target.Target != ShaderGraphTarget::Material)
                        throw std::runtime_error(
                            "This legacy graph does not describe a material-compatible surface program.");
                    pending.ShaderDefinition.GeneratedAssetOwner = item.SourceAsset;
                    if (!material)
                        pending.MaterialDefinition = PropertyMaterial(
                            CreateMaterialDefinition(pending.ShaderDefinition, item.GeneratedShaderAsset));
                    pending.MaterialDefinition.Shader.Kind = MaterialShaderSourceKind::ShaderGraph;
                    pending.MaterialDefinition.Shader.Asset = item.GeneratedShaderAsset;
                    Json shaderSubAssets = Json::array();
                    Json materialSubAssets = Json::array();
                    const auto oldSubAssets = pending.MaterialMetadata.value("subAssets", Json::array());
                    if (!oldSubAssets.is_array())
                        throw std::invalid_argument("Material metadata subassets must be an array.");
                    for (std::size_t index = 0; index < oldSubAssets.size(); ++index)
                        (index + 1 == oldSubAssets.size() ? materialSubAssets : shaderSubAssets)
                            .push_back(oldSubAssets[index]);
                    pending.ShaderMetadata = {
                        {"schemaVersion", 1},
                        {"id", item.GeneratedShaderAsset.ToString()},
                        {"type", ShaderGraphAsset::StaticType().Value().ToString()},
                        {"importer", "Keire.ShaderGraph"},
                        {"importerVersion", CreateShaderGraphAssetImporter().Version},
                        {"dependencies", pending.MaterialMetadata.value("dependencies", Json::array())},
                        {"subAssets", std::move(shaderSubAssets)}};
                    pending.MaterialMetadata["subAssets"] = std::move(materialSubAssets);
                    (void)ShaderGraphAsset::EncodeSource(pending.ShaderDefinition);
                }
                else
                    item.ShaderGraph.clear();
                (void)MaterialAsset::EncodeAuthoringSource(pending.MaterialDefinition);
                pending.MaterialMetadata["type"] = MaterialGraphAsset::StaticType().Value().ToString();
                pending.MaterialMetadata["importer"] = "Keire.MaterialGraph";
                pending.MaterialMetadata["importerVersion"] = CreateMaterialGraphAssetImporter().Version;
                if (pending.MaterialDefinition.Shader.Asset)
                {
                    auto dependencies = pending.MaterialMetadata.value("dependencies", Json::array());
                    if (!dependencies.is_array())
                        throw std::runtime_error("Material dependency metadata must be an array.");
                    const auto shader = pending.MaterialDefinition.Shader.Asset.ToString();
                    if (std::ranges::find(dependencies, Json(shader)) == dependencies.end())
                        dependencies.push_back(shader);
                    pending.MaterialMetadata["dependencies"] = std::move(dependencies);
                }
                item.Disposition = ShaderGraphMigrationDisposition::Migrate;
                item.Diagnostic = item.CreatesShader
                                      ? "Extract executable shader logic and retain a property-only material."
                                      : "Convert property bindings directly to a property-only material.";
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
            Json snapshot = Json::array();
            for (const auto& item : report.Items)
                snapshot.push_back({item.MaterialGraph.generic_string(), item.ShaderGraph.generic_string(),
                                    static_cast<unsigned>(item.Disposition), item.Diagnostic});
            for (const auto& candidate : pending)
            {
                snapshot.push_back(Detail::DigestToString(Detail::Sha256(candidate.OriginalSource)));
                snapshot.push_back(Detail::DigestToString(Detail::Sha256(candidate.OriginalMetadata)));
                if (candidate.Item.CreatesShader)
                    snapshot.push_back(Detail::DigestToString(
                        Detail::Sha256(ShaderGraphAsset::EncodeSource(candidate.ShaderDefinition))));
            }
            // A review also approves shader/include/texture dependencies and the current published generation.
            // Relative paths keep the fingerprint reproducible in the isolated validation snapshot.
            const Detail::AnchoredFileSystem files(root);
            std::vector<std::filesystem::path> inputs;
            constexpr std::array directories{"Assets", "Packages", "ProjectSettings"};
            for (const auto* directory : directories)
            {
                if (!files.Exists(directory))
                    continue;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(root / directory))
                {
                    if (entry.is_symlink())
                        throw std::runtime_error("Material migration review cannot include symbolic links.");
                    if (entry.is_directory())
                        continue;
                    const auto relative = entry.path().lexically_relative(root);
                    if (!files.IsRegularFile(relative))
                        throw std::runtime_error("Material migration review requires regular project files.");
                    inputs.push_back(relative);
                    if (inputs.size() > 4096)
                        throw std::runtime_error("Material migration review exceeds 4096 files.");
                }
            }
            for (const auto* publication :
                 {"Library/AssetCache/Runtime/catalog.json", "Library/AssetCache/Runtime/source-index.json"})
                if (files.Exists(publication))
                    inputs.emplace_back(publication);
            std::ranges::sort(inputs);
            std::size_t inputBytes = 0;
            for (const auto& input : inputs)
            {
                const auto bytes = files.Read(input, MaximumMigrationSourceBytes);
                inputBytes += bytes.size();
                if (inputBytes > 512U * 1024U * 1024U)
                    throw std::runtime_error("Material migration review exceeds 512 MiB of project inputs.");
                snapshot.push_back({input.generic_string(), Detail::DigestToString(Detail::Sha256(bytes))});
            }
            const auto text = snapshot.dump();
            report.ReviewFingerprint =
                Detail::DigestToString(Detail::Sha256(std::as_bytes(std::span(text.data(), text.size()))));
            return {std::move(report), std::move(pending)};
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
        return ApplyShaderGraphMigration(projectRoot, InspectShaderGraphMigration(projectRoot));
    }

    ShaderGraphMigrationReport ApplyShaderGraphMigration(const std::filesystem::path& projectRoot,
                                                         const ShaderGraphMigrationReport& reviewed)
    {
        auto [report, pending] = Inspect(projectRoot);
        if (reviewed.ReviewFingerprint.empty() || reviewed.ReviewFingerprint != report.ReviewFingerprint)
            throw std::runtime_error("Material migration inputs changed after review; inspect the upgrade again.");
        if (!report.CanApply())
            throw std::runtime_error(
                "Shader Graph migration has conflicts or invalid legacy assets; no files changed.");
        if (pending.empty())
            return report;

        const auto root = std::filesystem::absolute(projectRoot).lexically_normal();
        std::vector<Detail::ProjectFileReplacement> files;
        const auto jsonBytes = [](const Json& value)
        {
            const auto text = value.dump(2) + '\n';
            const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
            return std::vector<std::byte>(bytes.begin(), bytes.end());
        };
        for (const auto& migration : pending)
        {
            const auto material = std::filesystem::path("Assets") / migration.Item.MaterialGraph;
            files.push_back({material, migration.OriginalSource,
                             MaterialAsset::EncodeAuthoringSource(migration.MaterialDefinition)});
            files.push_back(
                {MetadataPath(material), migration.OriginalMetadata, jsonBytes(migration.MaterialMetadata)});
            if (migration.Item.CreatesShader)
            {
                const auto shader = std::filesystem::path("Assets") / migration.Item.ShaderGraph;
                files.push_back({shader, std::nullopt, ShaderGraphAsset::EncodeSource(migration.ShaderDefinition)});
                files.push_back({MetadataPath(shader), std::nullopt, jsonBytes(migration.ShaderMetadata)});
            }
        }
        Detail::PublishMaterialMigrationFiles(root, files);
        return report;
    }

    std::size_t RecoverShaderGraphMigration(const std::filesystem::path& projectRoot)
    {
        return Detail::RecoverMaterialMigrationFiles(projectRoot);
    }
} // namespace Keire
