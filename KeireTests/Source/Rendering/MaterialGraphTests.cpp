#include "Keire/Project/ShaderGraphMigration.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace
{
    [[nodiscard]] Keire::MaterialGraphDefinition SampleMaterialGraph()
    {
        Keire::MaterialGraphDefinition result;
        result.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
        result.Shader.Asset = Keire::AssetId::Parse("a1000000-0000-4000-8000-000000000001");
        result.Shader.Target = "default";
        result.Shader.Keywords.emplace("QUALITY", "HIGH");
        result.Surface.AlphaMode = Keire::MaterialAlphaMode::Mask;
        result.Surface.AlphaCutoff = 0.35F;
        result.Properties.push_back({Keire::AssetId::Parse("a2000000-0000-4000-8000-000000000001"), "Tint",
                                     Keire::Color{0.2F, 0.4F, 0.8F, 1.0F}});
        result.Properties.push_back(
            {Keire::AssetId::Parse("a2000000-0000-4000-8000-000000000002"), "Roughness", 0.45F});
        return result;
    }
} // namespace

TEST_CASE("Material Graph source and cooked serialization are deterministic")
{
    const auto definition = SampleMaterialGraph();
    const auto source = Keire::MaterialGraphAsset::EncodeSource(definition);
    CHECK(Keire::MaterialGraphAsset::EncodeSource(Keire::MaterialGraphAsset::DecodeSource(source)) == source);
    const auto cooked = Keire::MaterialGraphAsset::Encode(definition);
    CHECK(Keire::MaterialGraphAsset::Decode(cooked)->Definition() == definition);

    auto duplicate = definition;
    duplicate.Properties.push_back(duplicate.Properties.front());
    CHECK_THROWS_AS(Keire::ValidateMaterialGraph(duplicate), std::invalid_argument);
}

TEST_CASE("Material Graph bindings retain stable shader property identities across renames")
{
    const auto definition = SampleMaterialGraph();
    Keire::ShaderInterfaceDefinition interfaceDefinition;
    Keire::ShaderPropertyDefinition tint;
    tint.Id = definition.Properties[0].Property;
    tint.Name = "BaseColor";
    tint.Type = Keire::ShaderPropertyType::Color;
    interfaceDefinition.Properties.push_back(tint);
    Keire::ShaderPropertyDefinition roughness;
    roughness.Id = definition.Properties[1].Property;
    roughness.Name = "Roughness";
    roughness.Type = Keire::ShaderPropertyType::Scalar;
    interfaceDefinition.Properties.push_back(roughness);

    const auto diagnostics = Keire::ValidateMaterialGraphAgainstInterface(definition, interfaceDefinition);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().Severity == Keire::MaterialGraphDiagnosticSeverity::Info);
    CHECK(diagnostics.front().Code == "MAT1003");
}

TEST_CASE("Material Graph baking resolves an exact Shader Graph variant and publishes a runtime material")
{
    const auto definition = SampleMaterialGraph();
    const auto expectedShader = Keire::AssetId::Parse("a3000000-0000-4000-8000-000000000001");
    const auto material = Keire::BakeMaterialGraph(definition,
                                                   [&](const Keire::MaterialShaderReference& reference)
                                                   {
                                                       CHECK(reference == definition.Shader);
                                                       return expectedShader;
                                                   });
    CHECK(material.Shader == expectedShader);
    CHECK(material.Surface == definition.Surface);
    CHECK(std::get<float>(material.Properties.at("Roughness")) == doctest::Approx(0.45F));

    const auto importer = Keire::CreateMaterialGraphAssetImporter();
    const auto materialId = Keire::AssetId::Parse("a4000000-0000-4000-8000-000000000001");
    const auto variantOwner = Keire::AssetId::Parse("a4500000-0000-4000-8000-000000000001");
    auto shaderGraph = Keire::CreateDefaultShaderGraph();
    shaderGraph.GeneratedAssetOwner = variantOwner;
    const auto shaderGraphBytes = Keire::ShaderGraphAsset::EncodeSource(shaderGraph);
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("a5000000-0000-4000-8000-000000000001");
    context.ProjectRoot = "C:/MaterialGraphImportTest";
    context.SourceRoot = context.ProjectRoot / "Assets";
    context.ReadProjectFile = [shaderGraphBytes](const std::filesystem::path& path)
    {
        CHECK(path.generic_string() == "Assets/Graphs/Surface.keireshadergraph");
        return shaderGraphBytes;
    };
    context.ResolveAssetSource =
        [graphAsset = definition.Shader.Asset](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset == graphAsset)
            return Keire::AssetImportSource{asset, Keire::ShaderGraphAsset::StaticType(),
                                            "Graphs/Surface.keireshadergraph"};
        return std::nullopt;
    };
    context.ResolveSubAssetId = [=](std::string_view key)
    {
        CHECK(key == "material/default");
        return materialId;
    };
    context.ResolveSubAssetIdFor = [=](Keire::AssetId graph, std::string_view key)
    {
        CHECK(graph == variantOwner);
        CHECK(key == Keire::MakeShaderGraphVariantSubAssetKey("default", definition.Shader.Keywords));
        return expectedShader;
    };
    const auto output = importer.ContextualImport(context, Keire::MaterialGraphAsset::EncodeSource(definition));
    REQUIRE(output.SubAssets.size() == 1);
    CHECK(output.SubAssets.front().Id == materialId);
    CHECK(Keire::MaterialAsset::Decode(output.SubAssets.front().Bytes)->Definition().Shader == expectedShader);
    CHECK(std::ranges::find(output.AssetDependencies, definition.Shader.Asset) != output.AssetDependencies.end());
    CHECK(std::ranges::find(output.AssetDependencies, expectedShader) != output.AssetDependencies.end());
}

TEST_CASE("Direct material authoring supports tagged raw and Shader Graph references")
{
    Keire::MaterialAuthoringDefinition source;
    source.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    source.Shader.Asset = Keire::AssetId::Parse("b1000000-0000-4000-8000-000000000001");
    source.Shader.Keywords.emplace("USE_DETAIL", "true");
    source.Properties.emplace("Tint", Keire::Color{0.1F, 0.2F, 0.3F, 1.0F});
    const auto bytes = Keire::MaterialAsset::EncodeAuthoringSource(source);
    CHECK(Keire::MaterialAsset::DecodeAuthoringSource(bytes) == source);

    const auto resolved = Keire::AssetId::Parse("b2000000-0000-4000-8000-000000000001");
    const auto variantOwner = Keire::AssetId::Parse("b1500000-0000-4000-8000-000000000001");
    auto shaderGraph = Keire::CreateDefaultShaderGraph();
    shaderGraph.GeneratedAssetOwner = variantOwner;
    const auto shaderGraphBytes = Keire::ShaderGraphAsset::EncodeSource(shaderGraph);
    Keire::AssetImportContext context;
    context.ProjectRoot = "C:/DirectMaterialImportTest";
    context.SourceRoot = context.ProjectRoot / "Assets";
    context.ReadProjectFile = [shaderGraphBytes](const std::filesystem::path& path)
    {
        CHECK(path.generic_string() == "Assets/Graphs/Surface.keireshadergraph");
        return shaderGraphBytes;
    };
    context.ResolveAssetSource =
        [graphAsset = source.Shader.Asset](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset == graphAsset)
            return Keire::AssetImportSource{asset, Keire::ShaderGraphAsset::StaticType(),
                                            "Graphs/Surface.keireshadergraph"};
        return std::nullopt;
    };
    context.ResolveSubAssetIdFor = [=](Keire::AssetId parent, std::string_view key)
    {
        CHECK(parent == variantOwner);
        CHECK(key == Keire::MakeShaderGraphVariantSubAssetKey("default", source.Shader.Keywords));
        return resolved;
    };
    const auto imported = Keire::CreateMaterialAssetImporter().ContextualImport(context, bytes);
    CHECK(Keire::MaterialAsset::Decode(imported.Bytes)->Definition().Shader == resolved);
}

TEST_CASE("Legacy procedural Material Graph migration is transactional and preserves published identities")
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("keire-shader-graph-migration-" + Keire::AssetId::Generate().ToString());
    struct Cleanup final
    {
        std::filesystem::path Root;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } cleanup{root};
    const auto materialPath = root / "Assets/Materials/Paint.keirematerialgraph";
    std::filesystem::create_directories(materialPath.parent_path());
    const auto legacy = Keire::CreateDefaultShaderGraph();
    const auto legacyBytes = Keire::ShaderGraphAsset::EncodeSource(legacy);
    std::ofstream(materialPath, std::ios::binary)
        .write(reinterpret_cast<const char*>(legacyBytes.data()), static_cast<std::streamsize>(legacyBytes.size()));
    const auto materialId = Keire::AssetId::Parse("c1000000-0000-4000-8000-000000000001");
    const auto shaderId = Keire::AssetId::Parse("c2000000-0000-4000-8000-000000000001");
    const auto runtimeMaterialId = Keire::AssetId::Parse("c3000000-0000-4000-8000-000000000001");
    nlohmann::json metadata{{"schemaVersion", 1},
                            {"id", materialId.ToString()},
                            {"type", Keire::MaterialGraphAsset::StaticType().ToString()},
                            {"importer", "Keire.MaterialGraph"},
                            {"importerVersion", 14},
                            {"dependencies", nlohmann::json::array()},
                            {"subAssets", {shaderId.ToString(), runtimeMaterialId.ToString()}}};
    std::ofstream(materialPath.string() + ".keiremeta") << metadata.dump(2) << '\n';

    const auto check = Keire::InspectShaderGraphMigration(root);
    REQUIRE(check.CanApply());
    CHECK(check.PendingCount() == 1);
    const auto applied = Keire::ApplyShaderGraphMigration(root);
    CHECK(applied.PendingCount() == 1);

    const auto read = [](const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        return std::vector<std::byte>{reinterpret_cast<const std::byte*>(text.data()),
                                      reinterpret_cast<const std::byte*>(text.data() + text.size())};
    };
    const auto migratedMaterial = Keire::MaterialGraphAsset::DecodeSource(read(materialPath));
    const auto shaderPath = root / "Assets/Materials/Paint_Shader.keireshadergraph";
    const auto migratedShader = Keire::ShaderGraphAsset::DecodeSource(read(shaderPath));
    CHECK(migratedShader.GeneratedAssetOwner == materialId);
    CHECK(migratedMaterial.Shader.Asset);
    const auto materialMetadata = nlohmann::json::parse(std::ifstream(materialPath.string() + ".keiremeta"));
    const auto shaderMetadata = nlohmann::json::parse(std::ifstream(shaderPath.string() + ".keiremeta"));
    CHECK(materialMetadata.at("id").get<std::string>() == materialId.ToString());
    CHECK(materialMetadata.at("subAssets").at(0).get<std::string>() == runtimeMaterialId.ToString());
    CHECK(shaderMetadata.at("subAssets").at(0).get<std::string>() == shaderId.ToString());
    CHECK(shaderMetadata.at("id").get<std::string>() == migratedMaterial.Shader.Asset.ToString());
    CHECK(Keire::InspectShaderGraphMigration(root).PendingCount() == 0);
}

TEST_CASE("Legacy Material Graph migration rejects destination conflicts without changing source assets")
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("keire-shader-graph-conflict-" + Keire::AssetId::Generate().ToString());
    struct Cleanup final
    {
        std::filesystem::path Root;
        ~Cleanup()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    } cleanup{root};
    const auto materialPath = root / "Assets/Materials/Paint.keirematerialgraph";
    const auto shaderPath = root / "Assets/Materials/Paint_Shader.keireshadergraph";
    std::filesystem::create_directories(materialPath.parent_path());
    const auto legacyBytes = Keire::ShaderGraphAsset::EncodeSource(Keire::CreateDefaultShaderGraph());
    std::ofstream(materialPath, std::ios::binary)
        .write(reinterpret_cast<const char*>(legacyBytes.data()), static_cast<std::streamsize>(legacyBytes.size()));
    const nlohmann::json metadata{
        {"schemaVersion", 1},
        {"id", Keire::AssetId::Generate().ToString()},
        {"type", Keire::MaterialGraphAsset::StaticType().ToString()},
        {"importer", "Keire.MaterialGraph"},
        {"importerVersion", 14},
        {"dependencies", nlohmann::json::array()},
        {"subAssets", {Keire::AssetId::Generate().ToString(), Keire::AssetId::Generate().ToString()}}};
    std::ofstream(materialPath.string() + ".keiremeta") << metadata.dump(2) << '\n';
    constexpr std::string_view sentinel = "existing shader graph\n";
    std::ofstream(shaderPath, std::ios::binary).write(sentinel.data(), static_cast<std::streamsize>(sentinel.size()));

    const auto report = Keire::InspectShaderGraphMigration(root);
    CHECK_FALSE(report.CanApply());
    CHECK(report.PendingCount() == 0);
    REQUIRE(report.Items.size() == 1);
    CHECK(report.Items.front().Disposition == Keire::ShaderGraphMigrationDisposition::Conflict);
    CHECK_THROWS_AS((void)Keire::ApplyShaderGraphMigration(root), std::runtime_error);

    std::ifstream unchanged(materialPath, std::ios::binary);
    const std::string unchangedText{std::istreambuf_iterator<char>(unchanged), std::istreambuf_iterator<char>()};
    const std::span unchangedBytes{reinterpret_cast<const std::byte*>(unchangedText.data()), unchangedText.size()};
    CHECK(std::ranges::equal(unchangedBytes, legacyBytes));
    std::ifstream existing(shaderPath, std::ios::binary);
    const std::string existingText{std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>()};
    CHECK(existingText == sentinel);
}
