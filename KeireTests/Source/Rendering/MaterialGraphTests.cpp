#include "Keire/Project/ShaderGraphMigration.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    [[nodiscard]] std::vector<std::byte> ReadAssetSource(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Could not open Material Graph test asset: " + path.string());
        const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        return {reinterpret_cast<const std::byte*>(text.data()),
                reinterpret_cast<const std::byte*>(text.data() + text.size())};
    }

    [[nodiscard]] Keire::MaterialGraphDefinition SampleMaterialGraph()
    {
        Keire::MaterialShaderReference shader;
        shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
        shader.Asset = Keire::AssetId::Parse("a1000000-0000-4000-8000-000000000001");
        shader.Target = "default";
        shader.Keywords.emplace("QUALITY", "HIGH");
        Keire::ShaderInterfaceDefinition shaderInterface;
        Keire::ShaderPropertyDefinition tint;
        tint.Id = Keire::AssetId::Parse("a2000000-0000-4000-8000-000000000001");
        tint.Name = "Tint";
        tint.Type = Keire::ShaderPropertyType::Color;
        tint.DefaultValue = {0.2F, 0.4F, 0.8F, 1.0F};
        shaderInterface.Properties.push_back(tint);
        Keire::ShaderPropertyDefinition roughness;
        roughness.Id = Keire::AssetId::Parse("a2000000-0000-4000-8000-000000000002");
        roughness.Name = "Roughness";
        roughness.Type = Keire::ShaderPropertyType::Scalar;
        roughness.DefaultValue.X = 0.45F;
        shaderInterface.Properties.push_back(roughness);
        auto result = Keire::CreateMaterialGraph(std::move(shader), shaderInterface);
        result.Surface.AlphaMode = Keire::MaterialAlphaMode::Mask;
        result.Surface.AlphaCutoff = 0.35F;
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

    auto synchronized = definition;
    const auto tintPin = synchronized.Properties.front().Pin;
    Keire::SynchronizeMaterialGraphInterface(synchronized, interfaceDefinition);
    CHECK(synchronized.Properties.front().Name == "BaseColor");
    CHECK(synchronized.Properties.front().Pin == tintPin);
    CHECK(std::get<Keire::Color>(synchronized.Properties.front().Value) == Keire::Color{0.2F, 0.4F, 0.8F, 1.0F});
    CHECK(Keire::ValidateMaterialGraphAgainstInterface(synchronized, interfaceDefinition).empty());
}

TEST_CASE("Material Graph visual connections drive reflected Material Output inputs")
{
    auto definition = SampleMaterialGraph();
    auto value = Keire::CreateMaterialGraphValueNode(Keire::ShaderPropertyType::Scalar, 0.8F, {80.0F, 160.0F});
    value.Name = "Roughness Value";
    const auto node = value.Id;
    const auto outputPin = value.OutputPin;
    definition.Nodes.push_back(std::move(value));
    const auto input = definition.Properties[1].Pin;
    definition.Connections.push_back({Keire::AssetId::Generate(), {node, outputPin}, {definition.OutputNode, input}});

    Keire::ValidateMaterialGraph(definition);
    const auto evaluated = Keire::EvaluateMaterialGraphProperties(definition);
    CHECK(std::get<float>(evaluated.at("Roughness")) == doctest::Approx(0.8F));
    CHECK(Keire::MaterialGraphAsset::DecodeSource(Keire::MaterialGraphAsset::EncodeSource(definition)) == definition);

    Keire::ShaderInterfaceDefinition changedInterface;
    Keire::ShaderPropertyDefinition tint;
    tint.Id = definition.Properties[0].Property;
    tint.Name = "Tint";
    tint.Type = Keire::ShaderPropertyType::Color;
    changedInterface.Properties.push_back(tint);
    Keire::ShaderPropertyDefinition changedRoughness;
    changedRoughness.Id = definition.Properties[1].Property;
    changedRoughness.Name = "Roughness";
    changedRoughness.Type = Keire::ShaderPropertyType::Color;
    changedInterface.Properties.push_back(changedRoughness);
    Keire::SynchronizeMaterialGraphInterface(definition, changedInterface);
    CHECK(definition.Connections.empty());
    CHECK(definition.Properties[1].Type == Keire::ShaderPropertyType::Color);
    CHECK(std::holds_alternative<Keire::Color>(definition.Properties[1].Value));
    Keire::ValidateMaterialGraph(definition);
}

TEST_CASE("Material Graph schema one sources upgrade to deterministic visual topology")
{
    constexpr std::string_view legacy = R"({
  "schemaVersion": 1,
  "shader": {
    "kind": "graph",
    "asset": "a1000000-0000-4000-8000-000000000001",
    "target": "default",
    "keywords": {}
  },
  "properties": [
    {
      "id": "a2000000-0000-4000-8000-000000000002",
      "name": "Roughness",
      "type": 0,
      "value": 0.45
    }
  ]
})";
    const std::span bytes{reinterpret_cast<const std::byte*>(legacy.data()), legacy.size()};
    const auto first = Keire::MaterialGraphAsset::DecodeSource(bytes);
    const auto second = Keire::MaterialGraphAsset::DecodeSource(bytes);
    CHECK(first.SchemaVersion == Keire::MaterialGraphSourceSchemaVersion);
    CHECK(first.OutputNode);
    REQUIRE(first.Properties.size() == 1);
    CHECK(first.Properties.front().Pin);
    CHECK(first == second);
    CHECK(Keire::MaterialGraphAsset::DecodeSource(Keire::MaterialGraphAsset::EncodeSource(first)) == first);
}

TEST_CASE("Sandbox and packaged template ship current Material Graph sources")
{
    const auto repository = std::filesystem::current_path();
    const auto sandbox = repository / "Samples/KeireSandbox/Assets/Materials/MaterialGraphs";
    const auto packaged = repository / "KeireHubContent/Templates/Payloads/Sandbox/Assets/Materials/MaterialGraphs";
    std::size_t graphCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator(sandbox))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".keirematerialgraph")
            continue;
        const auto source = ReadAssetSource(entry.path());
        const auto definition = Keire::MaterialGraphAsset::DecodeSource(source);
        CHECK(definition.SchemaVersion == Keire::MaterialGraphSourceSchemaVersion);
        CHECK(definition.Shader.Kind == Keire::MaterialShaderSourceKind::ShaderGraph);
        CHECK(definition.OutputNode);
        CHECK(definition.Nodes.size() == definition.Properties.size());
        CHECK(definition.Connections.size() == definition.Properties.size());
        CHECK(ReadAssetSource(packaged / entry.path().filename()) == source);
        ++graphCount;
    }
    CHECK(graphCount == 9);
}

TEST_CASE("Material Instances serialize deterministically and enforce inherited interfaces")
{
    Keire::MaterialAssetDefinition parent;
    parent.Shader = Keire::AssetId::Parse("a6000000-0000-4000-8000-000000000001");
    parent.Properties.emplace("Roughness", 0.4F);
    parent.Properties.emplace("Tint", Keire::Color{1.0F, 1.0F, 1.0F, 1.0F});
    Keire::MaterialInstanceDefinition instance;
    instance.Parent = Keire::AssetId::Parse("a7000000-0000-4000-8000-000000000001");
    instance.Properties.emplace("Roughness", 0.75F);
    instance.Surface = Keire::MaterialSurfaceState{Keire::MaterialAlphaMode::Mask, 0.3F, true};

    const auto source = Keire::MaterialInstanceAsset::EncodeSource(instance);
    CHECK(Keire::MaterialInstanceAsset::EncodeSource(Keire::MaterialInstanceAsset::DecodeSource(source)) == source);
    CHECK(Keire::MaterialInstanceAsset::Decode(Keire::MaterialInstanceAsset::Encode(instance))->Definition() ==
          instance);
    const auto baked = Keire::BakeMaterialInstance(parent, instance);
    CHECK(std::get<float>(baked.Properties.at("Roughness")) == doctest::Approx(0.75F));
    CHECK(baked.Surface == *instance.Surface);

    auto unknown = instance;
    unknown.Properties.emplace("NotExposed", 1.0F);
    CHECK_THROWS_AS((void)Keire::BakeMaterialInstance(parent, unknown), std::invalid_argument);
    auto wrongType = instance;
    wrongType.Properties.insert_or_assign("Roughness", Keire::Color{});
    CHECK_THROWS_AS((void)Keire::BakeMaterialInstance(parent, wrongType), std::invalid_argument);
}

TEST_CASE("Material Instance importing resolves inherited Material roots without duplicating shader code")
{
    const auto rootAsset = Keire::AssetId::Parse("a8000000-0000-4000-8000-000000000001");
    const auto parentInstanceAsset = Keire::AssetId::Parse("a8000000-0000-4000-8000-000000000002");
    const auto childInstanceAsset = Keire::AssetId::Parse("a8000000-0000-4000-8000-000000000003");
    const auto runtimeMaterial = Keire::AssetId::Parse("a8000000-0000-4000-8000-000000000004");
    const auto shader = Keire::AssetId::Parse("a8000000-0000-4000-8000-000000000005");
    Keire::MaterialAuthoringDefinition root;
    root.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderAsset;
    root.Shader.Asset = shader;
    root.Properties.emplace("Roughness", 0.25F);
    root.Properties.emplace("Tint", Keire::Color{1.0F, 1.0F, 1.0F, 1.0F});
    Keire::MaterialInstanceDefinition parent;
    parent.Parent = rootAsset;
    parent.Properties.emplace("Roughness", 0.6F);
    Keire::MaterialInstanceDefinition child;
    child.Parent = parentInstanceAsset;
    child.Properties.emplace("Tint", Keire::Color{0.1F, 0.3F, 0.8F, 1.0F});
    const auto rootBytes = Keire::MaterialAsset::EncodeAuthoringSource(root);
    const auto parentBytes = Keire::MaterialInstanceAsset::EncodeSource(parent);

    Keire::AssetImportContext context;
    context.Asset = childInstanceAsset;
    context.ProjectRoot = "C:/MaterialInstanceImportTest";
    context.SourceRoot = context.ProjectRoot / "Assets";
    context.ReadProjectFile = [&](const std::filesystem::path& path)
    {
        if (path.generic_string() == "Assets/Materials/Root.keirematerial")
            return rootBytes;
        if (path.generic_string() == "Assets/Materials/Parent.keirematerialinstance")
            return parentBytes;
        throw std::runtime_error("Unexpected Material Instance test path: " + path.generic_string());
    };
    context.ResolveAssetSource = [&](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset == rootAsset)
            return Keire::AssetImportSource{asset, Keire::MaterialAsset::StaticType(), "Materials/Root.keirematerial"};
        if (asset == parentInstanceAsset)
            return Keire::AssetImportSource{asset, Keire::MaterialInstanceAsset::StaticType(),
                                            "Materials/Parent.keirematerialinstance"};
        return std::nullopt;
    };
    context.ResolveSubAssetId = [&](const std::string_view key)
    {
        CHECK(key == "material/default");
        return runtimeMaterial;
    };
    context.ResolveSubAssetIdFor = [](const Keire::AssetId, const std::string_view) { return Keire::AssetId{}; };

    const auto output = Keire::CreateMaterialInstanceAssetImporter().ContextualImport(
        context, Keire::MaterialInstanceAsset::EncodeSource(child));
    REQUIRE(output.SubAssets.size() == 1);
    CHECK(output.SubAssets.front().Id == runtimeMaterial);
    const auto material = Keire::MaterialAsset::Decode(output.SubAssets.front().Bytes)->Definition();
    CHECK(material.Shader == shader);
    CHECK(std::get<float>(material.Properties.at("Roughness")) == doctest::Approx(0.6F));
    CHECK(std::get<Keire::Color>(material.Properties.at("Tint")) == Keire::Color{0.1F, 0.3F, 0.8F, 1.0F});
    CHECK(std::ranges::find(output.AssetDependencies, rootAsset) != output.AssetDependencies.end());
    CHECK(std::ranges::find(output.AssetDependencies, parentInstanceAsset) != output.AssetDependencies.end());
    CHECK(std::ranges::find(output.AssetDependencies, shader) != output.AssetDependencies.end());

    auto cycleParent = parent;
    cycleParent.Parent = childInstanceAsset;
    const auto cycleBytes = Keire::MaterialInstanceAsset::EncodeSource(cycleParent);
    context.ReadProjectFile = [cycleBytes](const std::filesystem::path& path)
    {
        if (path.generic_string() == "Assets/Materials/Parent.keirematerialinstance")
            return cycleBytes;
        throw std::runtime_error("Unexpected Material Instance cycle path: " + path.generic_string());
    };
    CHECK_THROWS_AS(Keire::CreateMaterialInstanceAssetImporter().ContextualImport(
                        context, Keire::MaterialInstanceAsset::EncodeSource(child)),
                    std::invalid_argument);
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
    Keire::ShaderGraphKeyword quality;
    quality.Name = "QUALITY";
    quality.Options = {"LOW", "HIGH"};
    quality.DefaultOption = "HIGH";
    shaderGraph.Keywords.push_back(std::move(quality));
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
    source.Properties.emplace("Tint", Keire::Color{0.1F, 0.2F, 0.3F, 1.0F});
    const auto bytes = Keire::MaterialAsset::EncodeAuthoringSource(source);
    CHECK(Keire::MaterialAsset::DecodeAuthoringSource(bytes) == source);

    const auto resolved = Keire::AssetId::Parse("b2000000-0000-4000-8000-000000000001");
    const auto variantOwner = Keire::AssetId::Parse("b1500000-0000-4000-8000-000000000001");
    auto shaderGraph = Keire::CreateDefaultShaderGraph();
    shaderGraph.GeneratedAssetOwner = variantOwner;
    Keire::ShaderGraphKeyword detail;
    detail.Name = "USE_DETAIL";
    detail.DefaultOption = "true";
    shaderGraph.Keywords.push_back(std::move(detail));
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
        const std::array enabled{std::string("USE_DETAIL")};
        CHECK(key == Keire::MakeShaderGraphVariantSubAssetKey("default", enabled));
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
