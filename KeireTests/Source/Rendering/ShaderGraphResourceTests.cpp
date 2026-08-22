#include "Keire/Rendering/ShaderGraph.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] Keire::ShaderGraphResourceDefinition Resource(const Keire::ShaderGraphResourceKind kind,
                                                                const std::string& symbol,
                                                                Keire::ShaderGraphResourceValue value)
    {
        return {Keire::AssetId::Generate(), symbol, symbol, kind, std::move(value)};
    }

    [[nodiscard]] std::vector<Keire::ShaderGraphResourceDefinition> SampleResources()
    {
        Keire::SamplerDescription sampler;
        sampler.Minimum = Keire::TextureFilter::Nearest;
        sampler.Magnification = Keire::TextureFilter::Linear;
        sampler.Mip = Keire::TextureFilter::Nearest;
        sampler.AddressU = Keire::TextureAddressMode::Clamp;
        sampler.AddressV = Keire::TextureAddressMode::Mirror;
        sampler.AddressW = Keire::TextureAddressMode::Repeat;
        sampler.Anisotropy = 8;
        const auto sharedTexture = Keire::AssetId::Parse("a1000000-0000-4000-8000-000000000001");
        const auto buffer = Keire::AssetId::Parse("b1000000-0000-4000-8000-000000000001");
        return {Resource(Keire::ShaderGraphResourceKind::Sampler, "SurfaceSampler", sampler),
                Resource(Keire::ShaderGraphResourceKind::Texture2DArray, "LayerTextures", sharedTexture),
                Resource(Keire::ShaderGraphResourceKind::TextureCube, "ReflectionTexture", sharedTexture),
                Resource(Keire::ShaderGraphResourceKind::Texture3D, "VolumeTexture", Keire::AssetId{}),
                Resource(Keire::ShaderGraphResourceKind::StructuredBuffer, "SurfaceRecords",
                         Keire::ShaderGraphBufferView{buffer, 64, 1024, 16}),
                Resource(Keire::ShaderGraphResourceKind::ByteAddressBuffer, "PackedRecords",
                         Keire::ShaderGraphBufferView{buffer, 4096, 256, 0})};
    }
} // namespace

TEST_CASE("Shader Graph resource contract roundtrips portable values deterministically")
{
    const auto resources = SampleResources();
    const auto encoded = Keire::EncodeShaderGraphResources(resources);
    const auto decoded = Keire::DecodeShaderGraphResources(encoded);

    CHECK(decoded == resources);
    CHECK(Keire::EncodeShaderGraphResources(decoded) == encoded);
    const auto analysis = Keire::AnalyzeShaderGraphResources(decoded);
    CHECK(analysis.Succeeded());
    CHECK(analysis.Statistics.ResourceCount == 6);
    CHECK(analysis.Statistics.SamplerCount == 1);
    CHECK(analysis.Statistics.TextureCount == 3);
    CHECK(analysis.Statistics.ReadOnlyBufferCount == 2);
    CHECK(analysis.Statistics.StructuredBufferCount == 1);
    CHECK(analysis.Statistics.ByteAddressBufferCount == 1);
    CHECK(analysis.Statistics.BufferViewBytes == 1280);
}

TEST_CASE("Shader Graph schema four embeds resources in source generation and manifest reflection")
{
    auto graph = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    graph.Resources = SampleResources();
    const auto source = Keire::ShaderGraphAsset::EncodeSource(graph);
    const auto decoded = Keire::ShaderGraphAsset::DecodeSource(source);
    CHECK(decoded.Resources == graph.Resources);
    const auto cooked = Keire::ShaderGraphAsset::Encode(graph);
    CHECK(Keire::ShaderGraphAsset::Decode(cooked)->Definition().Resources == graph.Resources);

    Keire::ShaderGraphCompileOptions options;
    options.AllowOfflineResourceDeclarations = true;
    const auto compilation = Keire::CompileShaderGraph(decoded, options);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Variants.size() == 1);
    CHECK(compilation.Variants.front().Hlsl.find("TextureCube ReflectionTexture") != std::string::npos);
    const auto manifest = nlohmann::json::parse(compilation.Variants.front().Manifest);
    CHECK(manifest.at("resources").size() == graph.Resources.size());
    const auto reflected =
        Keire::ShaderAsset::DecodeManifest(std::as_bytes(std::span(compilation.Variants.front().Manifest)));
    CHECK(reflected.UserResourceSlots == 3);
    CHECK(reflected.UserReadOnlyBuffers == 2);
    const auto runtimeCompilation = Keire::CompileShaderGraph(decoded);
    CHECK_FALSE(runtimeCompilation.Succeeded());
}

TEST_CASE("Shader Graph resources emit independent portable t and s declarations")
{
    const auto declarations = Keire::GenerateShaderGraphResourceDeclarations(SampleResources(), 4, 11);

    CHECK(declarations.NextTextureRegister == 7);
    CHECK(declarations.NextSamplerRegister == 12);
    CHECK(declarations.NextBufferRegister == 2);
    CHECK(declarations.Hlsl.find("SamplerState SurfaceSampler : register(s11, space2);") != std::string::npos);
    CHECK(declarations.Hlsl.find("Texture2DArray LayerTextures : register(t4, space2);") != std::string::npos);
    CHECK(declarations.Hlsl.find("TextureCube ReflectionTexture : register(t5, space2);") != std::string::npos);
    CHECK(declarations.Hlsl.find("Texture3D VolumeTexture : register(t6, space2);") != std::string::npos);
    CHECK(declarations.Hlsl.find("StructuredBuffer<uint4> SurfaceRecords : register(t0, space5);") !=
          std::string::npos);
    CHECK(declarations.Hlsl.find("ByteAddressBuffer PackedRecords : register(t1, space5);") != std::string::npos);
}

TEST_CASE("Shader Graph resource dependencies are sorted and deduplicated")
{
    const auto resources = SampleResources();
    const auto dependencies = Keire::ShaderGraphResourceDependencies(resources);

    REQUIRE(dependencies.size() == 2);
    CHECK(std::ranges::is_sorted(dependencies));
}

TEST_CASE("Shader Graph material resource bindings resolve defaults and validate overrides")
{
    const auto resources = SampleResources();
    const auto replacement = Keire::AssetId::Parse("d1000000-0000-4000-8000-000000000001");
    const Keire::ShaderGraphResourceBindings overrides{{"ReflectionTexture", replacement}};
    const auto resolved = Keire::ResolveShaderGraphResourceBindings(resources, overrides);
    CHECK(std::get<Keire::AssetId>(resolved.at("ReflectionTexture")) == replacement);
    CHECK(resolved.at("SurfaceSampler") == resources.front().Value);

    const auto dependencies = Keire::ShaderGraphResourceBindingDependencies(resources, overrides);
    CHECK(std::ranges::find(dependencies, replacement) != dependencies.end());
    CHECK_THROWS_AS((void)Keire::ResolveShaderGraphResourceBindings(resources, {{"Missing", replacement}}),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::ResolveShaderGraphResourceBindings(
                        resources, {{"ReflectionTexture", Keire::SamplerDescription{}}}),
                    std::invalid_argument);
}

TEST_CASE("Shader Graph resource validation rejects sampler and buffer boundary violations")
{
    auto resources = SampleResources();
    auto invalidSampler = std::get<Keire::SamplerDescription>(resources.front().Value);
    invalidSampler.Anisotropy = 0;
    resources.front().Value = invalidSampler;
    CHECK_THROWS_AS(Keire::ValidateShaderGraphResources(resources), std::invalid_argument);
    CHECK_FALSE(Keire::AnalyzeShaderGraphResources(resources).Succeeded());

    resources = SampleResources();
    auto& structured = std::get<Keire::ShaderGraphBufferView>(resources[4].Value);
    structured.OffsetBytes = 2;
    CHECK_THROWS_AS(Keire::ValidateShaderGraphResources(resources), std::invalid_argument);

    resources = SampleResources();
    std::get<Keire::ShaderGraphBufferView>(resources[4].Value).SizeBytes =
        Keire::MaximumShaderGraphBufferViewBytes + 4U;
    CHECK_THROWS_AS(Keire::ValidateShaderGraphResources(resources), std::invalid_argument);

    resources = SampleResources();
    auto& byteAddress = std::get<Keire::ShaderGraphBufferView>(resources[5].Value);
    byteAddress.StrideBytes = 4;
    CHECK_THROWS_AS(Keire::ValidateShaderGraphResources(resources), std::invalid_argument);

    resources = SampleResources();
    auto& overflowing = std::get<Keire::ShaderGraphBufferView>(resources[5].Value);
    overflowing.OffsetBytes = std::numeric_limits<std::uint32_t>::max() - 3U;
    overflowing.SizeBytes = 4;
    CHECK_THROWS_AS(Keire::ValidateShaderGraphResources(resources), std::invalid_argument);
}

TEST_CASE("Shader Graph resource validation enforces the read-only buffer count")
{
    std::vector<Keire::ShaderGraphResourceDefinition> resources;
    const auto buffer = Keire::AssetId::Parse("c1000000-0000-4000-8000-000000000001");
    for (std::size_t index = 0; index < Keire::MaximumShaderGraphReadOnlyBuffers + 1U; ++index)
        resources.push_back(Resource(Keire::ShaderGraphResourceKind::ByteAddressBuffer,
                                     "Buffer" + std::to_string(index), Keire::ShaderGraphBufferView{buffer, 0, 4, 0}));

    CHECK_THROWS_AS(Keire::ValidateShaderGraphResources(resources), std::invalid_argument);
}

TEST_CASE("Shader Graph validation shares identities symbols and portable sampler slots with legacy parameters")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    graph.Resources = SampleResources();
    graph.Resources.front().Id = graph.Nodes.front().Id;
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);

    graph.Resources = SampleResources();
    auto parameter =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Texture2D);
    parameter.Name = "Surface sampler collision";
    parameter.Symbol = graph.Resources.front().Symbol;
    graph.Nodes.push_back(std::move(parameter));
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);

    graph = Keire::CreateDefaultShaderGraph();
    graph.Resources.clear();
    for (std::size_t index = 0; index < 13; ++index)
        graph.Resources.push_back(
            Resource(Keire::ShaderGraphResourceKind::TextureCube, "Cube" + std::to_string(index), Keire::AssetId{}));
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);
}

TEST_CASE("Shader Graph resource contract rejects future schemas before reading collections")
{
    const auto future = nlohmann::json{{"schemaVersion", Keire::ShaderGraphResourceContractSchemaVersion + 1U}}.dump();
    const auto bytes = std::as_bytes(std::span(future));

    CHECK_THROWS_WITH_AS((void)Keire::DecodeShaderGraphResources(bytes),
                         "Shader Graph resource contract schema version 2 is newer than the supported version 1.",
                         std::invalid_argument);
}
