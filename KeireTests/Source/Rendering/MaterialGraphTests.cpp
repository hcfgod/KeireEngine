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

    [[nodiscard]] Keire::ShaderGraphPin& GraphPin(Keire::ShaderGraphNode& node, const std::string_view name)
    {
        const auto found = std::ranges::find(node.Pins, name, &Keire::ShaderGraphPin::Name);
        if (found == node.Pins.end())
            throw std::logic_error("Material Graph test pin is unavailable.");
        return *found;
    }

    void ConnectGraph(Keire::ShaderGraphDefinition& graph, Keire::ShaderGraphNode& output,
                      const std::string_view outputPin, Keire::ShaderGraphNode& input, const std::string_view inputPin)
    {
        graph.Connections.push_back({Keire::AssetId::Generate(),
                                     {output.Id, GraphPin(output, outputPin).Id},
                                     {input.Id, GraphPin(input, inputPin).Id}});
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
    auto definition = SampleMaterialGraph();
    definition.Authoring.NodeAnnotations.push_back(
        {definition.OutputNode, "The immutable material output.", true, false});
    definition.Authoring.Comments.push_back({Keire::AssetId::Generate(),
                                             "Material output",
                                             "Reflected properties are connected here.",
                                             {-80.0F, -60.0F},
                                             {480.0F, 360.0F},
                                             {0.38F, 0.2F, 0.64F, 0.35F},
                                             18.0F,
                                             Keire::GraphCommentMoveMode::Group,
                                             {},
                                             {definition.OutputNode},
                                             false});
    const auto source = Keire::MaterialGraphAsset::EncodeSource(definition);
    CHECK(Keire::MaterialGraphAsset::EncodeSource(Keire::MaterialGraphAsset::DecodeSource(source)) == source);
    const auto cooked = Keire::MaterialGraphAsset::Encode(definition);
    CHECK(Keire::MaterialGraphAsset::Decode(cooked)->Definition() == definition);

    auto duplicate = definition;
    duplicate.Properties.push_back(duplicate.Properties.front());
    CHECK_THROWS_AS(Keire::ValidateMaterialGraph(duplicate), std::invalid_argument);
}

TEST_CASE("Material Graph schema three migrates in memory and explicit save publishes schema six")
{
    const auto definition = SampleMaterialGraph();
    const auto current = Keire::MaterialGraphAsset::EncodeSource(definition);
    auto legacy = nlohmann::json::parse(std::string(reinterpret_cast<const char*>(current.data()), current.size()));
    legacy["schemaVersion"] = 3;
    legacy.erase("authoring");
    const auto legacySurface = Keire::ShaderGraphAsset::EncodeSource(definition.SurfaceGraph);
    legacy["surfaceGraph"] =
        nlohmann::json::parse(std::string(reinterpret_cast<const char*>(legacySurface.data()), legacySurface.size()));
    const auto legacyText = legacy.dump();
    const auto migrated = Keire::MaterialGraphAsset::DecodeSource(std::as_bytes(std::span(legacyText)));

    CHECK(migrated.SchemaVersion == Keire::MaterialGraphSourceSchemaVersion);
    CHECK(migrated.Authoring == Keire::GraphAuthoringMetadata{});
    const auto saved = Keire::MaterialGraphAsset::EncodeSource(migrated);
    const auto savedJson =
        nlohmann::json::parse(std::string(reinterpret_cast<const char*>(saved.data()), saved.size()));
    CHECK(savedJson.at("schemaVersion") == Keire::MaterialGraphSourceSchemaVersion);
    CHECK(savedJson.contains("surfaceGraph"));
    CHECK_FALSE(savedJson.contains("legacySurfaceGraph"));
    CHECK(savedJson.contains("authoring"));
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
    definition.Connections.push_back(
        {Keire::AssetId::Generate(), {node, outputPin}, {definition.OutputNode, input}, {{240.0F, 180.0F}}});

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

TEST_CASE("Material Graph composes Unreal-style surface expressions through a Shader Graph template")
{
    auto definition = SampleMaterialGraph();
    auto artistTint =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
    artistTint.Name = "Artist Tint";
    artistTint.Symbol = "ArtistTint";
    artistTint.Value = Keire::Color{0.15F, 0.45F, 0.9F, 1.0F};
    artistTint.ParameterMetadata.Category = "Surface";
    definition.SurfaceGraph.Nodes.push_back(std::move(artistTint));
    ConnectGraph(definition.SurfaceGraph, definition.SurfaceGraph.Nodes.back(), "Value",
                 definition.SurfaceGraph.Nodes.front(), "BaseColor");
    auto shaderTemplate = Keire::CreateDefaultShaderGraph();
    auto baseColor =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
    baseColor.Id = definition.Properties[0].Property;
    baseColor.Name = "Tint";
    baseColor.Symbol = "Tint";
    auto roughness =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    roughness.Id = definition.Properties[1].Property;
    roughness.Name = "Roughness";
    roughness.Symbol = "Roughness";
    roughness.Value = 0.5F;
    shaderTemplate.Nodes.push_back(std::move(baseColor));
    shaderTemplate.Nodes.push_back(std::move(roughness));
    ConnectGraph(shaderTemplate, shaderTemplate.Nodes[1], "Value", shaderTemplate.Nodes.front(), "BaseColor");
    ConnectGraph(shaderTemplate, shaderTemplate.Nodes[2], "Value", shaderTemplate.Nodes.front(), "Roughness");

    const auto composed = Keire::ComposeMaterialGraphShader(definition, shaderTemplate);
    CHECK(
        std::ranges::none_of(composed.Nodes, [](const Keire::ShaderGraphNode& node)
                             { return node.Kind == Keire::ShaderGraphNodeKind::Parameter && node.Symbol == "Tint"; }));
    CHECK(std::ranges::any_of(
        composed.Nodes, [](const Keire::ShaderGraphNode& node)
        { return node.Kind == Keire::ShaderGraphNodeKind::Parameter && node.Symbol == "ArtistTint"; }));
    const auto compilation = Keire::CompileShaderGraph(composed);
    REQUIRE(compilation.Succeeded());
    CHECK(std::ranges::any_of(compilation.Properties, [](const Keire::ShaderPropertyDefinition& property)
                              { return property.Name == "ArtistTint" && property.Category == "Surface"; }));
    const auto composedRoughness =
        std::ranges::find(compilation.Properties, "Roughness", &Keire::ShaderPropertyDefinition::Name);
    REQUIRE(composedRoughness != compilation.Properties.end());
    CHECK(composedRoughness->DefaultValue.X == doctest::Approx(0.45F));
    CHECK(Keire::MaterialGraphAsset::DecodeSource(Keire::MaterialGraphAsset::EncodeSource(definition)) == definition);

    const auto saved = Keire::MaterialGraphAsset::EncodeSource(definition);
    const auto savedJson =
        nlohmann::json::parse(std::string(reinterpret_cast<const char*>(saved.data()), saved.size()));
    CHECK(savedJson.contains("surfaceGraph"));
    CHECK_FALSE(savedJson.contains("legacySurfaceGraph"));
}

TEST_CASE("Material Graph surface outputs inherit the selected Shader Graph template contract")
{
    constexpr std::array templates{Keire::ShaderGraphTemplate::Lit,         Keire::ShaderGraphTemplate::Unlit,
                                   Keire::ShaderGraphTemplate::Transparent, Keire::ShaderGraphTemplate::Decal,
                                   Keire::ShaderGraphTemplate::Fullscreen,  Keire::ShaderGraphTemplate::Hair,
                                   Keire::ShaderGraphTemplate::Eye};
    for (const auto graphTemplate : templates)
    {
        const auto shader = Keire::CreateShaderGraphTemplate(graphTemplate);
        const auto material = Keire::CreateMaterialSurfaceGraph(shader);
        CHECK(material.Output == shader.Output);
        REQUIRE(material.Nodes.size() == 1);
        CHECK(material.Nodes.front().Kind == Keire::ShaderGraphNodeKind::Master);
        CHECK(material.Nodes.front().Name == "Material Output");
        const auto output =
            std::ranges::find(shader.Nodes, Keire::ShaderGraphNodeKind::Master, &Keire::ShaderGraphNode::Kind);
        REQUIRE(output != shader.Nodes.end());
        REQUIRE(material.Nodes.front().Pins.size() == output->Pins.size());
        for (std::size_t index = 0; index < output->Pins.size(); ++index)
        {
            CHECK(material.Nodes.front().Pins[index].Name == output->Pins[index].Name);
            CHECK(material.Nodes.front().Pins[index].Type == output->Pins[index].Type);
            CHECK(material.Nodes.front().Pins[index].Id != output->Pins[index].Id);
        }
        CHECK_NOTHROW(Keire::ValidateShaderGraph(material));
    }
}

TEST_CASE("Material Graph importer publishes composed shader variants and instance parameters")
{
    auto definition = SampleMaterialGraph();
    auto artistTint =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
    artistTint.Name = "Artist Tint";
    artistTint.Symbol = "ArtistTint";
    artistTint.Value = Keire::Color{0.15F, 0.45F, 0.9F, 1.0F};
    artistTint.ParameterMetadata.Category = "Surface";
    definition.SurfaceGraph.Nodes.push_back(std::move(artistTint));
    ConnectGraph(definition.SurfaceGraph, definition.SurfaceGraph.Nodes.back(), "Value",
                 definition.SurfaceGraph.Nodes.front(), "BaseColor");

    auto shaderTemplate = Keire::CreateDefaultShaderGraph();
    auto tint = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
    tint.Id = definition.Properties[0].Property;
    tint.Name = "Tint";
    tint.Symbol = "Tint";
    auto roughness =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    roughness.Id = definition.Properties[1].Property;
    roughness.Name = "Roughness";
    roughness.Symbol = "Roughness";
    shaderTemplate.Nodes.push_back(std::move(tint));
    shaderTemplate.Nodes.push_back(std::move(roughness));
    ConnectGraph(shaderTemplate, shaderTemplate.Nodes[1], "Value", shaderTemplate.Nodes.front(), "BaseColor");
    ConnectGraph(shaderTemplate, shaderTemplate.Nodes[2], "Value", shaderTemplate.Nodes.front(), "Roughness");
    shaderTemplate.Keywords.push_back({"QUALITY", {"LOW", "HIGH"}, "HIGH", true});

    const auto lowShader = Keire::AssetId::Parse("a6000000-0000-4000-8000-000000000001");
    const auto highShader = Keire::AssetId::Parse("a6000000-0000-4000-8000-000000000002");
    const auto runtimeMaterial = Keire::AssetId::Parse("a6000000-0000-4000-8000-000000000003");
    const auto shaderBytes = Keire::ShaderGraphAsset::EncodeSource(shaderTemplate);
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("a6000000-0000-4000-8000-000000000004");
    context.ProjectRoot = "C:/MaterialExpressionImportTest";
    context.SourceRoot = context.ProjectRoot / "Assets";
    context.ReadProjectFile = [shaderBytes](const std::filesystem::path& path)
    {
        CHECK(path.generic_string() == "Assets/Graphs/Surface.keireshadergraph");
        return std::vector<std::byte>(shaderBytes);
    };
    context.ResolveAssetSource =
        [graphAsset = definition.Shader.Asset](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset == graphAsset)
            return Keire::AssetImportSource{asset, Keire::ShaderGraphAsset::StaticType(),
                                            "Graphs/Surface.keireshadergraph"};
        return std::nullopt;
    };
    context.ResolveSubAssetId = [=](const std::string_view key)
    {
        if (key == "material/default")
            return runtimeMaterial;
        if (key == "material-graph/" +
                       Keire::MakeShaderGraphVariantSubAssetKey("default", std::array{std::string("QUALITY_LOW")}))
            return lowShader;
        if (key == "material-graph/" +
                       Keire::MakeShaderGraphVariantSubAssetKey("default", std::array{std::string("QUALITY_HIGH")}))
            return highShader;
        throw std::logic_error("Unexpected composed Material Graph subasset key: " + std::string(key));
    };

    const auto output = Keire::CreateMaterialGraphAssetImporter().ContextualImport(
        context, Keire::MaterialGraphAsset::EncodeSource(definition));
    const auto material = std::ranges::find_if(
        output.SubAssets, [](const Keire::AssetGeneratedSubAsset& subAsset)
        { return subAsset.Type == Keire::MaterialAsset::StaticType() && subAsset.Key == "material/default"; });
    REQUIRE(material != output.SubAssets.end());
    const auto runtime = Keire::MaterialAsset::Decode(material->Bytes)->Definition();
    CHECK(runtime.Shader == highShader);
    CHECK(std::get<Keire::Color>(runtime.Properties.at("ArtistTint")) == Keire::Color{0.15F, 0.45F, 0.9F, 1.0F});
    CHECK(std::ranges::find(output.AssetDependencies, definition.Shader.Asset) != output.AssetDependencies.end());
}

TEST_CASE("Material Instances override parameters exposed by composed Material Graphs")
{
    auto definition = SampleMaterialGraph();
    definition.Shader.Keywords.clear();
    auto artistTint =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
    artistTint.Name = "Artist Tint";
    artistTint.Symbol = "ArtistTint";
    artistTint.Value = Keire::Color{0.2F, 0.4F, 0.8F, 1.0F};
    definition.SurfaceGraph.Nodes.push_back(std::move(artistTint));
    ConnectGraph(definition.SurfaceGraph, definition.SurfaceGraph.Nodes.back(), "Value",
                 definition.SurfaceGraph.Nodes.front(), "BaseColor");

    auto staticDetail =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Keyword, Keire::ShaderGraphValueType::Scalar);
    staticDetail.Name = "Static Detail";
    staticDetail.Symbol = "STATIC_DETAIL";
    definition.SurfaceGraph.Keywords.push_back({"STATIC_DETAIL", {}, "false", true});
    definition.SurfaceGraph.Nodes.push_back(std::move(staticDetail));
    ConnectGraph(definition.SurfaceGraph, definition.SurfaceGraph.Nodes.back(), "Enabled",
                 definition.SurfaceGraph.Nodes.front(), "Metallic");

    auto shaderTemplate = Keire::CreateDefaultShaderGraph();
    auto tint = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
    tint.Id = definition.Properties[0].Property;
    tint.Name = "Tint";
    tint.Symbol = "Tint";
    auto roughness =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    roughness.Id = definition.Properties[1].Property;
    roughness.Name = "Roughness";
    roughness.Symbol = "Roughness";
    shaderTemplate.Nodes.push_back(std::move(tint));
    shaderTemplate.Nodes.push_back(std::move(roughness));
    ConnectGraph(shaderTemplate, shaderTemplate.Nodes[1], "Value", shaderTemplate.Nodes.front(), "BaseColor");
    ConnectGraph(shaderTemplate, shaderTemplate.Nodes[2], "Value", shaderTemplate.Nodes.front(), "Roughness");

    const auto parentGraph = Keire::AssetId::Parse("a6500000-0000-4000-8000-000000000001");
    const auto instanceAsset = Keire::AssetId::Parse("a6500000-0000-4000-8000-000000000002");
    const auto parentDefaultShader = Keire::AssetId::Parse("a6500000-0000-4000-8000-000000000003");
    const auto parentShader = Keire::AssetId::Parse("a6500000-0000-4000-8000-000000000006");
    const auto parentMaterial = Keire::AssetId::Parse("a6500000-0000-4000-8000-000000000004");
    const auto instanceMaterial = Keire::AssetId::Parse("a6500000-0000-4000-8000-000000000005");
    Keire::MaterialInstanceDefinition instance;
    instance.Parent = parentGraph;
    instance.Properties.emplace("ArtistTint", Keire::Color{0.9F, 0.15F, 0.35F, 1.0F});
    instance.KeywordOverrides.emplace("STATIC_DETAIL", "true");
    const auto graphBytes = Keire::MaterialGraphAsset::EncodeSource(definition);
    const auto shaderBytes = Keire::ShaderGraphAsset::EncodeSource(shaderTemplate);

    Keire::AssetImportContext context;
    context.Asset = instanceAsset;
    context.ProjectRoot = "C:/ComposedMaterialInstanceTest";
    context.SourceRoot = context.ProjectRoot / "Assets";
    context.ReadProjectFile = [graphBytes, shaderBytes](const std::filesystem::path& path)
    {
        if (path.generic_string() == "Assets/Materials/Parent.keirematerialgraph")
            return std::vector<std::byte>(graphBytes);
        if (path.generic_string() == "Assets/Graphs/Surface.keireshadergraph")
            return std::vector<std::byte>(shaderBytes);
        throw std::logic_error("Unexpected composed Material Instance source: " + path.generic_string());
    };
    context.ResolveAssetSource = [parentGraph, shaderGraph = definition.Shader.Asset](
                                     const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset == parentGraph)
            return Keire::AssetImportSource{asset, Keire::MaterialGraphAsset::StaticType(),
                                            "Materials/Parent.keirematerialgraph"};
        if (asset == shaderGraph)
            return Keire::AssetImportSource{asset, Keire::ShaderGraphAsset::StaticType(),
                                            "Graphs/Surface.keireshadergraph"};
        return std::nullopt;
    };
    context.ResolveSubAssetId = [instanceMaterial](const std::string_view key)
    {
        CHECK(key == "material/default");
        return instanceMaterial;
    };
    context.ResolveSubAssetIdFor = [=](const Keire::AssetId owner, const std::string_view key)
    {
        CHECK(owner == parentGraph);
        if (key == "material/default")
            return parentMaterial;
        const std::array<std::string, 0> disabled;
        if (key == "material-graph/" + Keire::MakeShaderGraphVariantSubAssetKey("default", disabled))
            return parentDefaultShader;
        const std::array enabled{std::string("STATIC_DETAIL")};
        CHECK(key == "material-graph/" + Keire::MakeShaderGraphVariantSubAssetKey("default", enabled));
        return parentShader;
    };

    const auto output = Keire::CreateMaterialInstanceAssetImporter().ContextualImport(
        context, Keire::MaterialInstanceAsset::EncodeSource(instance));
    REQUIRE(output.SubAssets.size() == 1);
    const auto material = Keire::MaterialAsset::Decode(output.SubAssets.front().Bytes)->Definition();
    CHECK(material.Shader == parentShader);
    CHECK(std::get<Keire::Color>(material.Properties.at("ArtistTint")) == Keire::Color{0.9F, 0.15F, 0.35F, 1.0F});
    CHECK(std::ranges::find(output.AssetDependencies, parentGraph) != output.AssetDependencies.end());
    CHECK(std::ranges::find(output.AssetDependencies, definition.Shader.Asset) != output.AssetDependencies.end());
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
    const auto sandbox = repository / "Samples/KeireSandbox/Assets/Examples/MaterialLab/MaterialGraphs";
    const auto packaged =
        repository / "KeireHubContent/Templates/Payloads/Sandbox/Assets/Examples/MaterialLab/MaterialGraphs";
    std::size_t graphCount = 0;
    std::size_t expressionGraphCount = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(sandbox))
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
        const auto output = std::ranges::find(definition.SurfaceGraph.Nodes, Keire::ShaderGraphNodeKind::Master,
                                              &Keire::ShaderGraphNode::Kind);
        REQUIRE(output != definition.SurfaceGraph.Nodes.end());
        if (std::ranges::any_of(definition.SurfaceGraph.Connections, [&](const Keire::ShaderGraphConnection& connection)
                                { return connection.Input.Node == output->Id; }))
            ++expressionGraphCount;
        CHECK(ReadAssetSource(packaged / entry.path().lexically_relative(sandbox)) == source);
        ++graphCount;
    }
    CHECK(graphCount == 12);
    CHECK(expressionGraphCount == graphCount);
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

    const auto legacy = std::string("{\"schemaVersion\":1,\"parent\":\"") + instance.Parent.ToString() +
                        "\",\"surface\":null,\"properties\":[]}";
    const std::span legacyBytes{reinterpret_cast<const std::byte*>(legacy.data()), legacy.size()};
    const auto upgraded = Keire::MaterialInstanceAsset::DecodeSource(legacyBytes);
    CHECK(upgraded.SchemaVersion == Keire::MaterialInstanceSourceSchemaVersion);
    CHECK(upgraded.KeywordOverrides.empty());
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
            return std::vector<std::byte>(rootBytes);
        if (path.generic_string() == "Assets/Materials/Parent.keirematerialinstance")
            return std::vector<std::byte>(parentBytes);
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
            return std::vector<std::byte>(cycleBytes);
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
    CHECK(importer.Version == 9);
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
        return std::vector<std::byte>(shaderGraphBytes);
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
        return std::vector<std::byte>(shaderGraphBytes);
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
