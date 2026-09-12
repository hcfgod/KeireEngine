#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Rendering/MaterialGraph.h"
#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

TEST_CASE("shader property descriptions and HDR metadata share graph manifest and cooked contracts")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto parameter =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Color);
    parameter.Symbol = "Radiance";
    parameter.Value = Keire::Color{4.0F, 2.0F, 0.5F, 1.0F};
    parameter.ParameterMetadata.Description = "Linear radiance; values may exceed one.";
    parameter.ParameterMetadata.HighDynamicRange = true;
    graph.Nodes.push_back(parameter);
    const auto decoded = Keire::ShaderGraphAsset::DecodeSource(Keire::ShaderGraphAsset::EncodeSource(graph));
    CHECK(decoded.Nodes.back().ParameterMetadata == parameter.ParameterMetadata);
    const auto compiled = Keire::CompileShaderGraph(decoded);
    REQUIRE(compiled.Succeeded());
    REQUIRE(compiled.Properties.size() == 1);
    CHECK(compiled.Properties[0].HighDynamicRange);
    CHECK(compiled.Properties[0].Description == parameter.ParameterMetadata.Description);
    REQUIRE_FALSE(compiled.Variants.empty());
    const auto& manifest = compiled.Variants[0].Manifest;
    auto reflected = Keire::ShaderAsset::DecodeManifest(std::as_bytes(std::span(manifest.data(), manifest.size())));
    CHECK(reflected.Properties[0].Description == parameter.ParameterMetadata.Description);
    CHECK(reflected.Properties[0].HighDynamicRange);
    for (const auto format :
         {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::Msl})
        reflected.Variants.push_back({format, {std::byte{1}}, {std::byte{2}}});
    const auto cooked = Keire::ShaderAsset::Decode(Keire::ShaderAsset::Encode(reflected));
    CHECK(cooked->Definition().Properties == reflected.Properties);
    reflected.Properties[0].Description.assign(513, 'x');
    CHECK_THROWS_AS((void)Keire::ShaderAsset::Encode(reflected), std::invalid_argument);
    reflected.Properties[0].Description.clear();
    reflected.Properties[0].Type = Keire::ShaderPropertyType::Scalar;
    CHECK_THROWS_AS((void)Keire::ShaderAsset::Encode(reflected), std::invalid_argument);
    graph.Nodes.back() =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    graph.Nodes.back().Symbol = "ScalarParameter";
    REQUIRE(Keire::CompileShaderGraph(graph).Succeeded());
    graph.Nodes.back().ParameterMetadata.HighDynamicRange = true;
    CHECK_FALSE(Keire::CompileShaderGraph(graph).Succeeded());
}

TEST_CASE("stable material overrides import current code symbols without compiling programs")
{
    const auto identity = Keire::AssetId::Generate();
    Keire::MaterialAuthoringDefinition source;
    source.SchemaVersion = 5;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.PropertyOverrides.push_back({identity, "BeforeRename", 0.75F});
    const auto manifest = std::string("{\"schemaVersion\":1,\"source\":\"Identity.hlsl\","
                                      "\"stages\":{\"vertex\":\"VSMain\",\"fragment\":\"PSMain\"},\"properties\":[") +
                          "{\"name\":\"AfterRename\",\"id\":\"" + identity.ToString() +
                          "\",\"type\":\"Float\",\"default\":[0,0,0,0]}]}";
    Keire::AssetImportContext context;
    context.ProjectRoot = "Project";
    context.SourceRoot = "Project/Assets";
    context.ResolveAssetSource = [&](const Keire::AssetId id) -> std::optional<Keire::AssetImportSource>
    {
        CHECK(id == source.Shader.Asset);
        return {{id, Keire::ShaderAsset::StaticType(), "Identity.keireshader"}};
    };
    context.ReadProjectFile = [&](const std::filesystem::path& path)
    {
        CHECK(path == std::filesystem::path("Assets/Identity.keireshader"));
        const auto bytes = std::as_bytes(std::span(manifest.data(), manifest.size()));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    const auto imported = Keire::CreateMaterialAssetImporter().ContextualImport(
        context, Keire::MaterialAsset::EncodeAuthoringSource(source));
    const auto material = Keire::MaterialAsset::Decode(imported.Bytes);
    CHECK(material->Definition().Shader == source.Shader.Asset);
    CHECK(std::get<float>(material->Definition().Properties.at("AfterRename")) == 0.75F);
    CHECK_FALSE(material->Definition().Properties.contains("BeforeRename"));
}

TEST_CASE("stable material property identity resolution is deterministic and has no name fallback")
{
    Keire::MaterialAuthoringDefinition source;
    source.SchemaVersion = 5;
    source.Shader.Asset = Keire::AssetId::Generate();
    source.PropertyOverrides.push_back({Keire::AssetId::Generate(), "ReusedName", 0.75F});
    Keire::ShaderAssetDefinition shader;
    shader.Source = "Identity.hlsl";
    shader.Properties = {{"ReusedName", Keire::ShaderPropertyType::Scalar}};
    shader.Properties.front().Id = Keire::AssetId::Generate();
    const auto bytes = Keire::MaterialAsset::EncodeAuthoringSource(source);
    const auto first = Keire::ResolveMaterialProperties(source, shader);
    const auto second = Keire::ResolveMaterialProperties(first.Authoring, shader);
    CHECK(first.Properties.empty());
    CHECK(first.InactiveOverrideIndices == std::vector<std::size_t>{0});
    CHECK(first.Authoring == second.Authoring);
    CHECK(first.InactiveProperties == second.InactiveProperties);
    CHECK(Keire::MaterialAsset::EncodeAuthoringSource(source) == bytes);
    shader.Properties.front().Id = source.PropertyOverrides.front().Property;
    shader.Properties.front().Name = "Recovered";
    const auto recovered = Keire::ResolveMaterialProperties(first.Authoring, shader);
    CHECK(std::get<float>(recovered.Properties.at("Recovered")) == 0.75F);
    CHECK(recovered.InactiveProperties.empty());
}

TEST_CASE("material variants override shader defaults by identity through nested ancestry")
{
    const auto graphId = Keire::AssetId::Generate();
    const auto rootId = Keire::AssetId::Generate();
    const auto parentId = Keire::AssetId::Generate();
    const auto childId = Keire::AssetId::Generate();
    const auto runtimeShader = Keire::AssetId::Generate();
    auto graph = Keire::CreateDefaultShaderGraph();
    auto property =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    property.Symbol = "RenamedValue";
    property.Value = 0.5F;
    property.ParameterMetadata.Minimum = 0.1F;
    property.ParameterMetadata.Maximum = 0.9F;
    graph.Nodes.push_back(property);
    Keire::MaterialAuthoringDefinition root;
    root.Shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    root.Shader.Asset = graphId;
    Keire::MaterialInstanceDefinition parent;
    parent.Parent = rootId;
    parent.PropertyOverrides.push_back({property.Id, "OldName", 0.25F});
    Keire::MaterialInstanceDefinition child;
    child.Parent = parentId;
    float expected = 0.25F;
    SUBCASE("child inherits parent's identity override") {}
    SUBCASE("nearest override wins")
    {
        child.PropertyOverrides.push_back({property.Id, "AnotherOldName", 0.75F});
        expected = 0.75F;
    }
    SUBCASE("removed identities stay inactive and do not replace inherited values")
    {
        child.PropertyOverrides.push_back({Keire::AssetId::Generate(), "RenamedValue", 1.0F});
    }
    CHECK(Keire::MaterialInstanceAsset::DecodeSource(Keire::MaterialInstanceAsset::EncodeSource(child)) == child);
    Keire::AssetImportContext context;
    context.Asset = childId;
    context.ProjectRoot = "Project";
    context.SourceRoot = "Project/Assets";
    context.ResolveSubAssetId = [](std::string_view) { return Keire::AssetId::Generate(); };
    context.ResolveSubAssetIdFor = [&](Keire::AssetId, std::string_view) { return runtimeShader; };
    context.ResolveAssetSource = [&](const Keire::AssetId id) -> std::optional<Keire::AssetImportSource>
    {
        if (id == graphId)
            return {{id, Keire::ShaderGraphAsset::StaticType(), "Shader.keireshadergraph"}};
        if (id == rootId)
            return {{id, Keire::MaterialGraphAsset::StaticType(), "Root.keirematerial"}};
        if (id == parentId)
            return {{id, Keire::MaterialInstanceAsset::StaticType(), "Parent.keirematerialinstance"}};
        return std::nullopt;
    };
    context.ReadProjectFile = [&](const std::filesystem::path& path)
    {
        if (path == std::filesystem::path("Assets/Shader.keireshadergraph"))
            return Keire::ShaderGraphAsset::EncodeSource(graph);
        if (path == std::filesystem::path("Assets/Root.keirematerial"))
            return Keire::MaterialAsset::EncodeAuthoringSource(root);
        CHECK(path == std::filesystem::path("Assets/Parent.keirematerialinstance"));
        return Keire::MaterialInstanceAsset::EncodeSource(parent);
    };
    const auto imported = Keire::CreateMaterialInstanceAssetImporter().ContextualImport(
        context, Keire::MaterialInstanceAsset::EncodeSource(child));
    REQUIRE(imported.SubAssets.size() == 1);
    const auto material = Keire::MaterialAsset::Decode(imported.SubAssets.front().Bytes);
    CHECK(std::get<float>(material->Definition().Properties.at("RenamedValue")) == expected);
    CHECK_FALSE(material->Definition().Properties.contains("OldName"));
}
