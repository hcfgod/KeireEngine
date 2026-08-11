#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Rendering/MaterialEcosystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    [[nodiscard]] const Keire::ShaderGraphPin& Pin(const Keire::ShaderGraphNode& node, const std::string_view name)
    {
        const auto found = std::ranges::find(node.Pins, name, &Keire::ShaderGraphPin::Name);
        if (found == node.Pins.end())
            throw std::logic_error("Material ecosystem test pin is unavailable.");
        return *found;
    }

    [[nodiscard]] Keire::ShaderGraphDefinition CallFunction(const Keire::AssetId asset,
                                                            const Keire::ShaderGraphDefinition& function)
    {
        auto graph = Keire::CreateDefaultShaderGraph();
        auto call = Keire::CreateShaderGraphFunctionCallNode(asset, function);
        const auto& master = graph.Nodes.front();
        graph.Connections.push_back(
            {Keire::AssetId::Generate(), {call.Id, Pin(call, "Result").Id}, {master.Id, Pin(master, "BaseColor").Id}});
        graph.Nodes.push_back(std::move(call));
        return graph;
    }
} // namespace

TEST_CASE("reusable shader graphs round trip with distinct purposes and stable dependencies")
{
    const auto material = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::MaterialFunction);
    const auto shader = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::ShaderFunction);
    const auto layer = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::MaterialLayer);
    const auto blend = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::MaterialLayerBlend);

    CHECK(Keire::MaterialFunctionAsset::DecodeSource(Keire::MaterialFunctionAsset::EncodeSource(material)) == material);
    CHECK(Keire::ShaderFunctionAsset::DecodeSource(Keire::ShaderFunctionAsset::EncodeSource(shader)) == shader);
    CHECK(Keire::MaterialLayerAsset::DecodeSource(Keire::MaterialLayerAsset::EncodeSource(layer)) == layer);
    CHECK(Keire::MaterialLayerBlendAsset::DecodeSource(Keire::MaterialLayerBlendAsset::EncodeSource(blend)) == blend);

    const auto functionId = Keire::AssetId::Parse("506c34ab-8347-4bc9-9062-21529279e8f2");
    const auto graph = CallFunction(functionId, material.Body);
    CHECK(Keire::ShaderGraphReferencedAssets(graph) == std::vector{functionId});
}

TEST_CASE("function calls expand deterministically and compile through the normal shader pipeline")
{
    const auto functionId = Keire::AssetId::Parse("f15d11a7-b04e-424a-b5c2-31df2002092e");
    const auto function = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::MaterialFunction);
    const auto graph = CallFunction(functionId, function.Body);
    const auto resolve = [&](const Keire::AssetId asset) -> std::optional<Keire::ShaderGraphDefinition>
    { return asset == functionId ? std::optional(function.Body) : std::nullopt; };

    const auto first = Keire::ExpandShaderGraphFunctions(graph, resolve);
    const auto second = Keire::ExpandShaderGraphFunctions(graph, resolve);
    CHECK(Keire::ShaderGraphAsset::EncodeSource(first) == Keire::ShaderGraphAsset::EncodeSource(second));
    CHECK(Keire::ShaderGraphReferencedAssets(first).empty());
    CHECK(std::ranges::none_of(first.Nodes,
                               [](const auto& node) { return node.Kind == Keire::ShaderGraphNodeKind::FunctionCall; }));

    Keire::ShaderGraphCompileOptions options;
    options.ResolveFunction = resolve;
    const auto compilation = Keire::CompileShaderGraph(graph, options);
    CHECK(compilation.Succeeded());
    CHECK(compilation.Variants.size() == 1);
}

TEST_CASE("Shader Graph import resolves reusable function assets and publishes their dependency closure")
{
    const auto functionId = Keire::AssetId::Parse("24edac13-e172-4f3c-aef3-b2ef7d6a8d15");
    const auto function = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::MaterialFunction);
    const auto functionBytes = Keire::MaterialFunctionAsset::EncodeSource(function);
    const auto graph = CallFunction(functionId, function.Body);
    const auto graphBytes = Keire::ShaderGraphAsset::EncodeSource(graph);

    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Parse("06838e57-0b80-4190-b404-7b8fd60b86a8");
    context.ProjectRoot = "Project";
    context.SourceRoot = "Project/Assets";
    context.SourcePath = "Project/Assets/Shaders/FunctionConsumer.keireshadergraph";
    context.RelativePath = "Shaders/FunctionConsumer.keireshadergraph";
    context.ReadProjectFile = [&](const std::filesystem::path& path)
    {
        if (path == std::filesystem::path("Assets/Functions/Common.keirematerialfunction"))
            return functionBytes;
        throw std::runtime_error("Unexpected reusable graph dependency path: " + path.generic_string());
    };
    context.ResolveAssetSource = [&](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset != functionId)
            return std::nullopt;
        return Keire::AssetImportSource{functionId, Keire::MaterialFunctionAsset::StaticType(),
                                        "Functions/Common.keirematerialfunction"};
    };
    std::map<std::string, Keire::AssetId, std::less<>> subAssets;
    context.ResolveSubAssetId = [&](const std::string_view key)
    { return subAssets.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
    context.ResolveSubAssetIdFor = [&](const Keire::AssetId owner, const std::string_view key)
    {
        return subAssets.try_emplace(owner.ToString() + '/' + std::string(key), Keire::AssetId::Generate())
            .first->second;
    };

    const auto importer = Keire::CreateShaderGraphAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport(context, graphBytes);
    CHECK(std::ranges::find(imported.AssetDependencies, functionId) != imported.AssetDependencies.end());
    CHECK(std::ranges::any_of(imported.SubAssets,
                              [](const auto& subAsset) { return subAsset.Type == Keire::ShaderAsset::StaticType(); }));
}

TEST_CASE("function expansion rejects missing assets and dependency cycles without mutating source graphs")
{
    const auto firstId = Keire::AssetId::Parse("85a49451-9b2a-4559-a369-1318226998a1");
    const auto secondId = Keire::AssetId::Parse("4c32167e-a5d8-416f-9126-3fed546a3100");
    auto first = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::MaterialFunction).Body;
    auto second = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::MaterialFunction).Body;
    first.Nodes.push_back(Keire::CreateShaderGraphFunctionCallNode(secondId, second));
    second.Nodes.push_back(Keire::CreateShaderGraphFunctionCallNode(firstId, first));
    const auto graph = CallFunction(firstId, first);
    const auto original = Keire::ShaderGraphAsset::EncodeSource(graph);

    CHECK_THROWS_AS((void)Keire::ExpandShaderGraphFunctions(graph, {}), std::invalid_argument);
    const std::map<Keire::AssetId, Keire::ShaderGraphDefinition> functions{{firstId, first}, {secondId, second}};
    CHECK_THROWS_AS((void)Keire::ExpandShaderGraphFunctions(
                        graph,
                        [&](const Keire::AssetId asset) -> std::optional<Keire::ShaderGraphDefinition>
                        {
                            const auto found = functions.find(asset);
                            return found == functions.end() ? std::nullopt : std::optional(found->second);
                        }),
                    std::invalid_argument);
    CHECK(Keire::ShaderGraphAsset::EncodeSource(graph) == original);
}

TEST_CASE("material parameter collections validate serialize and update runtime state transactionally")
{
    const auto wind = Keire::AssetId::Parse("c3cf2928-c685-4e53-970c-a5802b12bab4");
    Keire::MaterialParameterCollectionDefinition definition;
    definition.Parameters.push_back(
        {.Id = wind, .Name = "WindStrength", .DisplayName = "Wind Strength", .DefaultValue = 0.25F});
    const auto bytes = Keire::MaterialParameterCollectionAsset::EncodeSource(definition);
    CHECK(Keire::MaterialParameterCollectionAsset::DecodeSource(bytes) == definition);

    auto state = Keire::CreateRef<Keire::MaterialParameterCollectionState>(definition);
    const auto initialRevision = state->Revision();
    state->Set(wind, 0.25F);
    CHECK(state->Revision() == initialRevision);
    state->Set(wind, 0.75F);
    CHECK(std::get<float>(state->Snapshot().at(wind)) == doctest::Approx(0.75F));
    CHECK(state->Revision() == initialRevision + 1);
    CHECK(state->Reset(wind));
    CHECK(std::get<float>(state->Snapshot().at(wind)) == doctest::Approx(0.25F));
    CHECK_THROWS_AS(state->Set(wind, Keire::Vector3{}), std::invalid_argument);
    state->Close();
    CHECK_THROWS_AS((void)state->Snapshot(), std::logic_error);
}

TEST_CASE("dynamic material instances enforce inherited property types and preserve parent defaults")
{
    Keire::MaterialAssetDefinition parent;
    parent.Shader = Keire::AssetId::Parse("557919a9-1eba-4392-b26a-b64b97b7bff4");
    parent.Properties.emplace("Roughness", 0.4F);
    parent.Properties.emplace("Tint", Keire::Color{1.0F, 1.0F, 1.0F, 1.0F});
    auto instance = Keire::CreateRef<Keire::DynamicMaterialInstance>(parent);

    const auto initialRevision = instance->Revision();
    instance->SetProperty("Roughness", 0.4F);
    CHECK(instance->Revision() == initialRevision);
    instance->SetProperty("Roughness", 0.8F);
    CHECK(std::get<float>(instance->Snapshot().Properties.at("Roughness")) == doctest::Approx(0.8F));
    CHECK(instance->Revision() == initialRevision + 1);
    instance->SetProperty("Roughness", 0.8F);
    CHECK(instance->Revision() == initialRevision + 1);
    CHECK_THROWS_AS(instance->SetProperty("Roughness", Keire::Color{}), std::invalid_argument);
    CHECK_THROWS_AS(instance->SetProperty("Missing", 1.0F), std::invalid_argument);
    CHECK(instance->ResetProperty("Roughness"));
    CHECK(std::get<float>(instance->Snapshot().Properties.at("Roughness")) == doctest::Approx(0.4F));
    instance->Close();
    CHECK_THROWS_AS(instance->SetProperty("Roughness", 0.5F), std::logic_error);
}

TEST_CASE("built-in asset registration exposes every material ecosystem asset type")
{
    const auto importers = Keire::CreateBuiltinAssetImporters();
    const auto has = [&](const Keire::AssetTypeId type)
    { return std::ranges::any_of(importers, [type](const auto& importer) { return importer.Type == type; }); };
    CHECK(has(Keire::MaterialFunctionAsset::StaticType()));
    CHECK(has(Keire::ShaderFunctionAsset::StaticType()));
    CHECK(has(Keire::MaterialLayerAsset::StaticType()));
    CHECK(has(Keire::MaterialLayerBlendAsset::StaticType()));
    CHECK(has(Keire::MaterialParameterCollectionAsset::StaticType()));
}
