#include "Keire/Rendering/ShaderGraph.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    [[nodiscard]] Keire::ShaderGraphPin& Pin(Keire::ShaderGraphNode& node, const std::string_view name)
    {
        const auto found = std::ranges::find(node.Pins, name, &Keire::ShaderGraphPin::Name);
        if (found == node.Pins.end())
            throw std::logic_error("Test Shader Graph pin is unavailable.");
        return *found;
    }

    void Connect(Keire::ShaderGraphDefinition& graph, const Keire::ShaderGraphNode& output,
                 const std::string_view outputPin, const Keire::ShaderGraphNode& input, const std::string_view inputPin)
    {
        const auto outputFound = std::ranges::find_if(
            output.Pins, [outputPin](const Keire::ShaderGraphPin& pin)
            { return pin.Name == outputPin && pin.Direction == Keire::ShaderGraphPinDirection::Output; });
        const auto inputFound = std::ranges::find_if(
            input.Pins, [inputPin](const Keire::ShaderGraphPin& pin)
            { return pin.Name == inputPin && pin.Direction == Keire::ShaderGraphPinDirection::Input; });
        REQUIRE(outputFound != output.Pins.end());
        REQUIRE(inputFound != input.Pins.end());
        graph.Connections.push_back(
            {Keire::AssetId::Generate(), {output.Id, outputFound->Id}, {input.Id, inputFound->Id}});
    }

    [[nodiscard]] Keire::ShaderGraphNode
    Parameter(const std::string_view symbol, const Keire::ShaderGraphValueType type, Keire::ShaderGraphValue value)
    {
        auto result = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, type);
        result.Name = std::string(symbol);
        result.Symbol = symbol;
        result.Value = value;
        return result;
    }

    struct TemporaryDirectory final
    {
        TemporaryDirectory() : Path(KeireTests::MakeTestDirectory("ShaderGraphShaderImport"))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(Path, error);
        }

        std::filesystem::path Path;
    };

    void WriteText(const std::filesystem::path& path, const std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output)
            throw std::runtime_error("Cannot write Shader Graph test file.");
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto bytes = std::as_bytes(std::span(characters));
        return {bytes.begin(), bytes.end()};
    }
} // namespace

TEST_CASE("Shader Graph source and cooked assets preserve stable graph identity")
{
    auto definition = Keire::CreateDefaultShaderGraph();
    auto roughness = Parameter("Roughness", Keire::ShaderGraphValueType::Scalar, 0.42F);
    definition.Nodes.push_back(roughness);
    Connect(definition, definition.Nodes.back(), "Value", definition.Nodes.front(), "Roughness");
    definition.Connections.back().RoutingPoints = {{180.0F, 96.0F}, {320.0F, 132.0F}};
    definition.Authoring.NodeAnnotations.push_back({roughness.Id, "Driven by the finish mask.", true, true});
    definition.Authoring.Comments.push_back({Keire::AssetId::Generate(),
                                             "Surface response",
                                             "Parameters controlling the reflected light response.",
                                             {80.0F, 40.0F},
                                             {420.0F, 260.0F},
                                             {0.12F, 0.42F, 0.75F, 0.4F},
                                             20.0F,
                                             Keire::GraphCommentMoveMode::Group,
                                             {},
                                             {roughness.Id},
                                             false});

    const auto source = Keire::ShaderGraphAsset::EncodeSource(definition);
    const auto sourceDecoded = Keire::ShaderGraphAsset::DecodeSource(source);
    CHECK(sourceDecoded == definition);

    const auto cooked = Keire::ShaderGraphAsset::Encode(definition);
    const auto decoded = Keire::ShaderGraphAsset::Decode(cooked);
    CHECK(decoded->Definition() == definition);
    CHECK(Keire::ShaderGraphAsset::Encode(decoded->Definition()) == cooked);

    const auto importer = Keire::CreateShaderGraphAssetImporter();
    CHECK(importer.Name == "Keire.ShaderGraph");
    CHECK(importer.Version == 18);
    CHECK(importer.Extensions == std::vector<std::string>{".keireshadergraph"});
}

TEST_CASE("Shader Graph v2 catalogs stable node identities and migrates v1 sources")
{
    const auto catalog = Keire::ShaderGraphNodeCatalog();
    REQUIRE(catalog.size() >= 100);
    std::vector<std::string_view> typeIds;
    for (const auto& descriptor : catalog)
    {
        CHECK_FALSE(descriptor.TypeId.empty());
        CHECK(Keire::ShaderGraphNodeTypeId(descriptor.Kind) == descriptor.TypeId);
        CHECK(Keire::FindShaderGraphNodeDescriptor(descriptor.TypeId) == &descriptor);
        const auto node = Keire::CreateShaderGraphNode(descriptor.TypeId, descriptor.DefaultValueType);
        CHECK(node.Kind == descriptor.Kind);
        CHECK(node.TypeId == descriptor.TypeId);
        typeIds.push_back(descriptor.TypeId);
    }
    std::ranges::sort(typeIds);
    CHECK(std::ranges::adjacent_find(typeIds) == typeIds.end());
    CHECK(Keire::FindShaderGraphNodeDescriptor("keire.invalid.missing") == nullptr);
    CHECK_THROWS_AS((void)Keire::CreateShaderGraphNode("keire.invalid.missing"), std::invalid_argument);

    const auto definition = Keire::CreateDefaultShaderGraph();
    const auto encoded = Keire::ShaderGraphAsset::EncodeSource(definition);
    auto legacy = nlohmann::json::parse(std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
    legacy["schemaVersion"] = 1;
    for (auto& node : legacy["nodes"])
        node.erase("typeId");
    const auto legacyText = legacy.dump(2);
    const auto legacyBytes = std::as_bytes(std::span(legacyText));
    const auto migrated = Keire::ShaderGraphAsset::DecodeSource(legacyBytes);
    CHECK(migrated.SchemaVersion == Keire::ShaderGraphSourceSchemaVersion);
    REQUIRE(migrated.Nodes.size() == definition.Nodes.size());
    CHECK(migrated.Nodes.front().TypeId == "keire.output.material");

    auto previousV2 = nlohmann::json::parse(std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size()));
    auto& previousPins = previousV2["nodes"][0]["pins"];
    for (auto pin = previousPins.begin(); pin != previousPins.end();)
        pin = pin->at("name").get<std::string>() == "MaterialAttributes" ? previousPins.erase(pin) : std::next(pin);
    const auto previousV2Text = previousV2.dump(2);
    const auto previousV2Bytes = std::as_bytes(std::span(previousV2Text));
    const auto upgradedFirst = Keire::ShaderGraphAsset::DecodeSource(previousV2Bytes);
    const auto upgradedSecond = Keire::ShaderGraphAsset::DecodeSource(previousV2Bytes);
    const auto attributesFirst =
        std::ranges::find(upgradedFirst.Nodes.front().Pins, "MaterialAttributes", &Keire::ShaderGraphPin::Name);
    const auto attributesSecond =
        std::ranges::find(upgradedSecond.Nodes.front().Pins, "MaterialAttributes", &Keire::ShaderGraphPin::Name);
    REQUIRE(attributesFirst != upgradedFirst.Nodes.front().Pins.end());
    REQUIRE(attributesSecond != upgradedSecond.Nodes.front().Pins.end());
    CHECK(attributesFirst->Id == attributesSecond->Id);
}

TEST_CASE("Shader Graph compatibility versions are explicit and future sources fail recoverably")
{
    CHECK(Keire::ShaderGraphSourceSchemaVersion == 4);
    CHECK(Keire::ShaderGraphGeneratedShaderVersion == 5);
    CHECK(Keire::ShaderGraphVertexLayoutVersion == 3);

    const auto graph = Keire::CreateDefaultShaderGraph();
    const auto compilation = Keire::CompileShaderGraph(graph);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Variants.size() == 1);

    const auto& variant = compilation.Variants.front();
    const auto manifest = nlohmann::json::parse(variant.Manifest);
    CHECK(manifest.at("materialGraphSourceSchemaVersion") == Keire::ShaderGraphSourceSchemaVersion);
    CHECK(manifest.at("materialGraphGeneratedShaderVersion") == Keire::ShaderGraphGeneratedShaderVersion);
    CHECK(manifest.at("vertexLayoutVersion") == Keire::ShaderGraphVertexLayoutVersion);
    CHECK(manifest.at("instanceAddressingAbiVersion") == 2U);
    CHECK(manifest.at("occlusionSupport") == 3U);
    CHECK(variant.Hlsl.find("Generator version 5, source schema 4") != std::string::npos);
    CHECK(variant.Hlsl.find("cbuffer InstanceAddressingData : register(b2, space1)") != std::string::npos);
    CHECK(variant.Hlsl.find("uint4 InstanceParameters;") != std::string::npos);
    CHECK(variant.Hlsl.find("Instances[InstanceParameters.x + instanceId]") != std::string::npos);

    auto legacyManifest = manifest;
    legacyManifest.erase("instanceAddressingAbiVersion");
    legacyManifest.erase("occlusionSupport");
    const auto legacyManifestText = legacyManifest.dump();
    const auto legacyShader = Keire::ShaderAsset::DecodeManifest(std::as_bytes(std::span(legacyManifestText)));
    CHECK(legacyShader.InstanceAddressingAbiVersion == 0U);
    CHECK(legacyShader.OcclusionSupport == Keire::ShaderOcclusionSupport::None);

    auto invalidManifest = manifest;
    invalidManifest["instanceAddressingAbiVersion"] = 1U;
    const auto invalidManifestText = invalidManifest.dump();
    CHECK_THROWS_AS((void)Keire::ShaderAsset::DecodeManifest(std::as_bytes(std::span(invalidManifestText))),
                    std::invalid_argument);

    const auto future = nlohmann::json{{"schemaVersion", Keire::ShaderGraphSourceSchemaVersion + 1U}}.dump();
    const auto futureBytes = std::as_bytes(std::span(future));
    CHECK_THROWS_WITH_AS((void)Keire::ShaderGraphAsset::DecodeSource(futureBytes),
                         "Shader Graph schema version 5 is newer than the supported version 4.", std::invalid_argument);
}

TEST_CASE("Shader Graph v3 lowers multi-output nodes and parameter authoring metadata")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto tint = Parameter("AuthorTint", Keire::ShaderGraphValueType::Color, Keire::Color{0.2F, 0.4F, 0.8F, 1.0F});
    tint.ParameterMetadata.Description = "Primary art-directed surface tint.";
    tint.ParameterMetadata.Category = "Surface / Paint";
    tint.ParameterMetadata.SortPriority = 10;
    tint.ParameterMetadata.Minimum = 0.0F;
    tint.ParameterMetadata.Maximum = 1.0F;
    tint.ParameterMetadata.Step = 0.01F;
    auto mask =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::ComponentMask, Keire::ShaderGraphValueType::Vector4);
    graph.Nodes.insert(graph.Nodes.end(), {tint, mask});
    Connect(graph, tint, "Value", mask, "Value");
    Connect(graph, mask, "RGB", graph.Nodes.front(), "BaseColor");
    Connect(graph, mask, "R", graph.Nodes.front(), "Roughness");

    const auto compilation = Keire::CompileShaderGraph(graph);
    const auto diagnostic = compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
    INFO(diagnostic);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Properties.size() == 1);
    CHECK(compilation.Properties.front().Category == "Surface / Paint");
    CHECK(compilation.Properties.front().Minimum == 0.0F);
    CHECK(compilation.Properties.front().Maximum == 1.0F);
    CHECK(compilation.Properties.front().Step == 0.01F);
    REQUIRE(compilation.Variants.size() == 1);
    CHECK(compilation.Variants.front().Hlsl.find("(_KeireMaterial_AuthorTint).xyz") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("(_KeireMaterial_AuthorTint).x") != std::string::npos);
    CHECK(compilation.Variants.front().Manifest.find("Surface / Paint") != std::string::npos);
    CHECK(compilation.Variants.front().Manifest.find("\"minimum\"") != std::string::npos);

    mask.TypeId = "keire.math.add";
    graph.Nodes.back() = mask;
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);
}

TEST_CASE("Shader Graph compiles every output model to bounded runtime shader manifests")
{
    constexpr std::array outputs{Keire::ShaderGraphOutput::Surface,   Keire::ShaderGraphOutput::Transparent,
                                 Keire::ShaderGraphOutput::Decal,     Keire::ShaderGraphOutput::Unlit,
                                 Keire::ShaderGraphOutput::Hair,      Keire::ShaderGraphOutput::Eye,
                                 Keire::ShaderGraphOutput::Fullscreen};
    for (const auto output : outputs)
    {
        const auto graph = Keire::CreateDefaultShaderGraph(output);
        const auto compilation = Keire::CompileShaderGraph(graph);
        INFO(static_cast<int>(output));
        REQUIRE(compilation.Succeeded());
        REQUIRE(compilation.Variants.size() == 1);
        CHECK(compilation.Variants.front().Hlsl.find("VSMain") != std::string::npos);
        CHECK(compilation.Variants.front().Hlsl.find("PSMain") != std::string::npos);
        CHECK(compilation.Variants.front().Manifest.find("ShaderGraph-") != std::string::npos);
        if (output == Keire::ShaderGraphOutput::Transparent || output == Keire::ShaderGraphOutput::Decal)
            CHECK(compilation.Variants.front().Manifest.find("\"blend\": true") != std::string::npos);
        else
            CHECK(compilation.Variants.front().Manifest.find("\"blend\": false") != std::string::npos);
        const auto& manifest = compilation.Variants.front().Manifest;
        if (output == Keire::ShaderGraphOutput::Unlit || output == Keire::ShaderGraphOutput::Fullscreen)
        {
            CHECK(manifest.find("\"receivesShadows\": false") != std::string::npos);
            CHECK(manifest.find("\"usesForwardPlus\": false") != std::string::npos);
            CHECK(manifest.find("\"usesImageBasedLighting\": false") != std::string::npos);
        }
        else
        {
            CHECK(manifest.find("\"receivesShadows\": true") != std::string::npos);
            CHECK(manifest.find("\"usesForwardPlus\": true") != std::string::npos);
            CHECK(manifest.find("\"usesImageBasedLighting\": true") != std::string::npos);
        }
        if (output == Keire::ShaderGraphOutput::Hair)
            CHECK(manifest.find("\"culling\": \"None\"") != std::string::npos);
        if (output == Keire::ShaderGraphOutput::Fullscreen)
        {
            CHECK(manifest.find("\"culling\": \"None\"") != std::string::npos);
            CHECK(manifest.find("\"depthTest\": false") != std::string::npos);
            CHECK(manifest.find("\"depthWrite\": false") != std::string::npos);
        }
    }
}

TEST_CASE("Shader Graph creation templates map to deliberate output domains")
{
    const auto lit = Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Lit);
    REQUIRE(lit.Nodes.size() == 1);
    CHECK(lit.Output == Keire::ShaderGraphOutput::Surface);
    CHECK(lit.Nodes.front().Name == "Lit Shader Output");
    CHECK(Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Unlit).Output ==
          Keire::ShaderGraphOutput::Unlit);
    CHECK(Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Transparent).Output ==
          Keire::ShaderGraphOutput::Transparent);
    CHECK(Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Decal).Output ==
          Keire::ShaderGraphOutput::Decal);
    CHECK(Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Fullscreen).Output ==
          Keire::ShaderGraphOutput::Fullscreen);
    CHECK(Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Hair).Output == Keire::ShaderGraphOutput::Hair);
    CHECK(Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Eye).Output == Keire::ShaderGraphOutput::Eye);
}

TEST_CASE("Shader Graph generated HLSL compiles through the production shader importer")
{
    const auto repository = std::filesystem::current_path();
#if defined(_WIN32)
    const auto compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe";
#else
    const auto compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler";
#endif
    REQUIRE(std::filesystem::is_regular_file(compiler));

    Keire::ShaderGraphCompileOptions options;
    options.GeneratedSource = "Assets/Generated/ShaderGraphTest.hlsl";
    auto graph = Keire::CreateDefaultShaderGraph();
    auto texture = Parameter("PreviewTexture", Keire::ShaderGraphValueType::Texture2D, Keire::AssetId{});
    auto sample =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::TextureSample, Keire::ShaderGraphValueType::Color);
    auto roughness = Parameter("PreviewRoughness", Keire::ShaderGraphValueType::Scalar, 0.4F);
    auto uv = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::UV, Keire::ShaderGraphValueType::Vector2);
    auto noise =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::SimpleNoise, Keire::ShaderGraphValueType::Scalar);
    auto modulate =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Multiply, Keire::ShaderGraphValueType::Scalar);
    auto worldNormal =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::WorldNormal, Keire::ShaderGraphValueType::Vector3);
    auto fresnel =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Fresnel, Keire::ShaderGraphValueType::Scalar);
    auto vertexOffset =
        Parameter("VertexOffset", Keire::ShaderGraphValueType::Vector3, Keire::Vector3{0.0F, 0.0F, 0.0F});
    auto depthOffset = Parameter("DepthOffset", Keire::ShaderGraphValueType::Scalar, 0.0F);
    graph.Nodes.insert(graph.Nodes.end(), {texture, sample, roughness, uv, noise, modulate, worldNormal, fresnel,
                                           vertexOffset, depthOffset});
    Connect(graph, texture, "Value", sample, "Texture");
    Connect(graph, sample, "RGBA", graph.Nodes.front(), "BaseColor");
    Connect(graph, uv, "UV", noise, "UV");
    Connect(graph, noise, "Noise", modulate, "A");
    Connect(graph, roughness, "Value", modulate, "B");
    Connect(graph, modulate, "Result", graph.Nodes.front(), "Roughness");
    Connect(graph, worldNormal, "Vector", fresnel, "Normal");
    Connect(graph, fresnel, "Fresnel", graph.Nodes.front(), "ClearCoat");
    Connect(graph, vertexOffset, "Value", graph.Nodes.front(), "WorldPositionOffset");
    Connect(graph, depthOffset, "Value", graph.Nodes.front(), "PixelDepthOffset");
    const auto compilation = Keire::CompileShaderGraph(graph, options);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Variants.size() == 1);

    TemporaryDirectory directory;
    const auto source = directory.Path / compilation.Variants.front().GeneratedSource;
    auto manifest = source;
    manifest.replace_extension(".keireshader");
    WriteText(source, compilation.Variants.front().Hlsl);
    WriteText(manifest, compilation.Variants.front().Manifest);

    Keire::AssetImportContext context;
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path / "Assets";
    context.SourcePath = manifest;
    context.RelativePath = manifest.lexically_relative(context.SourceRoot);
    context.ReadProjectFile = [root = directory.Path](const std::filesystem::path& relative)
    { return ReadBytes(root / relative); };
    const auto importer = Keire::CreateShaderAssetImporter();
    CHECK(importer.Version == 3);
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport(context, ReadBytes(manifest));
    const auto shader = Keire::ShaderAsset::Decode(imported.Bytes);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::Dxil) != nullptr);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::Msl) != nullptr);
    CHECK(shader->Definition().InstanceAddressingAbiVersion == 2U);
    CHECK(shader->Definition().OcclusionSupport == Keire::ShaderOcclusionSupport::None);
    CHECK(imported.Diagnostics.empty());
    CHECK(compilation.Variants.front().Hlsl.find("MaterialNoise") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("MaterialValueNoise") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("graphClearCoat") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("EvaluateGraphDirectLighting") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("ForwardPlusLights") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find(
              "StructuredBuffer<ShaderGraphLocalLight> ForwardPlusLights : register(t5, space2)") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("StructuredBuffer<uint4> ForwardPlusTiles : register(t6, space2)") !=
          std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find(
              "StructuredBuffer<uint4> ForwardPlusLightIndices : register(t7, space2)") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("register(t16, space2)") == std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("EvaluateDirectionalShadow") != std::string::npos);
    const auto localParameters = compilation.Variants.front().Hlsl.find("float4 LocalShadowParameters[62]");
    const auto localSampleBounds = compilation.Variants.front().Hlsl.find("float4 LocalShadowSampleBounds[20]");
    REQUIRE(localParameters != std::string::npos);
    REQUIRE(localSampleBounds != std::string::npos);
    CHECK(localParameters < localSampleBounds);
    CHECK(compilation.Variants.front().Hlsl.find("clamp(unclampedUv, sampleBounds.xy, sampleBounds.zw)") !=
          std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("EvaluateDiffuseEnvironment") != std::string::npos);
    CHECK(compilation.Variants.front().Manifest.find("\"usesVertexMaterialParameters\": true") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("cbuffer VertexMaterialData : register(b1, space1)") !=
          std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("_KeireVertexMaterial_VertexOffset.xyz") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("SV_Depth") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("Keep the fixed interpolator ABI dense") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("SurfaceParameters.y > 0.5F") != std::string::npos);

    auto invalidBindingSource = compilation.Variants.front().Hlsl;
    const auto binding = invalidBindingSource.find("register(b2, space1)");
    REQUIRE(binding != std::string::npos);
    invalidBindingSource.replace(binding, std::string_view("register(b2, space1)").size(), "register(b3, space1)");
    WriteText(source, invalidBindingSource);
    CHECK_THROWS_WITH_AS((void)importer.ContextualImport(context, ReadBytes(manifest)),
                         "Shader instance-addressing ABI v2 requires uint4 InstanceParameters at vertex b2/space1.",
                         std::invalid_argument);
    invalidBindingSource +=
        "\n// cbuffer InstanceAddressingData : register(b2, space1) { uint4 InstanceParameters; };\n";
    WriteText(source, invalidBindingSource);
    CHECK_THROWS_WITH_AS((void)importer.ContextualImport(context, ReadBytes(manifest)),
                         "Shader instance-addressing ABI v2 requires uint4 InstanceParameters at vertex b2/space1.",
                         std::invalid_argument);
}

TEST_CASE("Shader Graph advanced node library lowers modern layered materials and reports cost")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto tint = Parameter("LayerTint", Keire::ShaderGraphValueType::Color, Keire::Color{0.12F, 0.32F, 0.54F, 1.0F});
    auto sheen = Parameter("SheenColor", Keire::ShaderGraphValueType::Color, Keire::Color{0.08F, 0.2F, 0.32F, 1.0F});
    auto uv = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::UV, Keire::ShaderGraphValueType::Vector2);
    auto rotate =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::RotateUV, Keire::ShaderGraphValueType::Vector2);
    auto noise =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::SimpleNoise, Keire::ShaderGraphValueType::Scalar);
    auto remap = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Remap, Keire::ShaderGraphValueType::Scalar);
    auto desaturate =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Desaturate, Keire::ShaderGraphValueType::Color);
    auto worldNormal =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::WorldNormal, Keire::ShaderGraphValueType::Vector3);
    auto fresnel =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Fresnel, Keire::ShaderGraphValueType::Scalar);
    graph.Nodes.insert(graph.Nodes.end(), {tint, sheen, uv, rotate, noise, remap, desaturate, worldNormal, fresnel});
    Connect(graph, uv, "UV", rotate, "UV");
    Connect(graph, rotate, "UV", noise, "UV");
    Connect(graph, noise, "Noise", remap, "Value");
    Connect(graph, tint, "Value", desaturate, "Color");
    Connect(graph, noise, "Noise", desaturate, "Amount");
    Connect(graph, desaturate, "Color", graph.Nodes.front(), "BaseColor");
    Connect(graph, remap, "Result", graph.Nodes.front(), "Roughness");
    Connect(graph, worldNormal, "Vector", fresnel, "Normal");
    Connect(graph, fresnel, "Fresnel", graph.Nodes.front(), "ClearCoat");
    Connect(graph, sheen, "Value", graph.Nodes.front(), "SheenColor");

    const auto compilation = Keire::CompileShaderGraph(graph);
    const auto diagnostic = compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
    INFO(diagnostic);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Statistics.NodeCount == graph.Nodes.size());
    CHECK(compilation.Statistics.ReachableNodeCount == graph.Nodes.size());
    CHECK(compilation.Statistics.UnusedNodeCount == 0);
    CHECK(compilation.Statistics.EstimatedAluInstructions > 48);
    CHECK(compilation.Statistics.VariantCount == 1);
    const auto& hlsl = compilation.Variants.front().Hlsl;
    CHECK(hlsl.find("RotateMaterialUV") != std::string::npos);
    CHECK(hlsl.find("MaterialNoise") != std::string::npos);
    CHECK(hlsl.find("DesaturateMaterialColor") != std::string::npos);
    CHECK(hlsl.find("graphSheenColor") != std::string::npos);
}

TEST_CASE("Shader Graph production node library lowers advanced coordinates sampling and surface utilities")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto texture = Parameter("AdvancedTexture", Keire::ShaderGraphValueType::Texture2D, Keire::AssetId{});
    auto uv = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::UV, Keire::ShaderGraphValueType::Vector2);
    auto time = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Time);
    auto panner = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Panner);
    auto noise = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::GradientNoise);
    auto heightNormal = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::HeightToNormal);
    auto wave = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Wave);
    auto worldPosition = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::WorldPosition);
    auto worldNormal = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::WorldNormal);
    auto triplanar = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::TriplanarSample);
    auto sampleLevel = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::TextureSampleLevel);
    auto overlay = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::BlendOverlay);
    auto facing = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::FacingRatio);
    auto blackbody = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Blackbody);
    auto screenPosition = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::ScreenPosition);
    auto dither = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Dither);
    graph.Nodes.insert(graph.Nodes.end(),
                       {texture, uv, time, panner, noise, heightNormal, wave, worldPosition, worldNormal, triplanar,
                        sampleLevel, overlay, facing, blackbody, screenPosition, dither});

    Connect(graph, uv, "UV", panner, "UV");
    Connect(graph, time, "Seconds", panner, "Time");
    Connect(graph, panner, "UV", noise, "UV");
    Connect(graph, noise, "Noise", heightNormal, "Height");
    Connect(graph, heightNormal, "Normal", graph.Nodes.front(), "Normal");
    Connect(graph, panner, "UV", wave, "UV");
    Connect(graph, wave, "Wave", graph.Nodes.front(), "Roughness");
    Connect(graph, texture, "Value", triplanar, "Texture");
    Connect(graph, worldPosition, "Vector", triplanar, "Position");
    Connect(graph, worldNormal, "Vector", triplanar, "Normal");
    Connect(graph, triplanar, "RGBA", overlay, "Base");
    Connect(graph, texture, "Value", sampleLevel, "Texture");
    Connect(graph, panner, "UV", sampleLevel, "UV");
    Connect(graph, sampleLevel, "RGBA", overlay, "Blend");
    Connect(graph, overlay, "Color", graph.Nodes.front(), "BaseColor");
    Connect(graph, worldNormal, "Vector", facing, "Normal");
    Connect(graph, facing, "Ratio", graph.Nodes.front(), "ClearCoat");
    Connect(graph, blackbody, "Color", graph.Nodes.front(), "Emission");
    Connect(graph, noise, "Noise", dither, "Alpha");
    Connect(graph, screenPosition, "UV", dither, "Screen Position");
    Connect(graph, dither, "Value", graph.Nodes.front(), "Opacity");

    const auto compilation = Keire::CompileShaderGraph(graph);
    const auto diagnostic = compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
    INFO(diagnostic);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Statistics.UnusedNodeCount == 0);
    CHECK(compilation.Statistics.TextureSampleCount == 4);
    const auto& hlsl = compilation.Variants.front().Hlsl;
    CHECK(hlsl.find(".SampleLevel(") != std::string::npos);
    CHECK(hlsl.find("MaterialOverlayBlend") != std::string::npos);
    CHECK(hlsl.find("MaterialBlackbody") != std::string::npos);
    CHECK(hlsl.find("MaterialDitherThreshold") != std::string::npos);
    CHECK(hlsl.find("ddx(") != std::string::npos);
}

TEST_CASE("Shader Graph composes typed material attributes and four production BSDF lobes")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto baseColor =
        Parameter("LayerBaseColor", Keire::ShaderGraphValueType::Color, Keire::Color{0.3F, 0.12F, 0.06F, 1.0F});
    auto standard = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::StandardSurfaceBsdf);
    auto clearCoat = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::ClearCoatBsdf);
    auto sheen = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::SheenBsdf);
    auto subsurface = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::SubsurfaceBsdf);
    auto transmission = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::TransmissionBsdf);
    auto attributes = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::BsdfToMaterialAttributes);
    auto breakAttributes = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::BreakMaterialAttributes);
    auto makeAttributes = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::MakeMaterialAttributes);
    auto blendAttributes = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::BlendMaterialAttributes);
    graph.Nodes.insert(graph.Nodes.end(), {baseColor, standard, clearCoat, sheen, subsurface, transmission, attributes,
                                           breakAttributes, makeAttributes, blendAttributes});

    Connect(graph, baseColor, "Value", standard, "BaseColor");
    Connect(graph, standard, "BSDF", clearCoat, "Base");
    Connect(graph, clearCoat, "BSDF", sheen, "Base");
    Connect(graph, sheen, "BSDF", subsurface, "Base");
    Connect(graph, subsurface, "BSDF", transmission, "Base");
    Connect(graph, transmission, "BSDF", attributes, "BSDF");
    Connect(graph, attributes, "Attributes", breakAttributes, "Attributes");
    Connect(graph, breakAttributes, "BaseColor", makeAttributes, "BaseColor");
    Connect(graph, breakAttributes, "Roughness", makeAttributes, "Roughness");
    Connect(graph, attributes, "Attributes", blendAttributes, "A");
    Connect(graph, makeAttributes, "Attributes", blendAttributes, "B");
    Connect(graph, blendAttributes, "Attributes", graph.Nodes.front(), "MaterialAttributes");

    const auto source = Keire::ShaderGraphAsset::EncodeSource(graph);
    const auto decoded = Keire::ShaderGraphAsset::DecodeSource(source);
    CHECK(decoded == graph);
    const auto compilation = Keire::CompileShaderGraph(decoded);
    const auto diagnostic = compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
    INFO(diagnostic);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Statistics.UnusedNodeCount == 0);
    const auto& hlsl = compilation.Variants.front().Hlsl;
    CHECK(hlsl.find("struct ShaderGraphSurface") != std::string::npos);
    CHECK(hlsl.find("MakeStandardShaderGraphBsdf") != std::string::npos);
    CHECK(hlsl.find("ApplyShaderGraphClearCoat") != std::string::npos);
    CHECK(hlsl.find("ApplyShaderGraphSheen") != std::string::npos);
    CHECK(hlsl.find("ApplyShaderGraphSubsurface") != std::string::npos);
    CHECK(hlsl.find("ApplyShaderGraphTransmission") != std::string::npos);
    CHECK(hlsl.find("BlendShaderGraphSurfaces") != std::string::npos);
    CHECK(hlsl.find("const ShaderGraphSurface graphMaterialAttributes") != std::string::npos);

    TemporaryDirectory directory;
    const auto generatedSource = directory.Path / compilation.Variants.front().GeneratedSource;
    auto manifestPath = generatedSource;
    manifestPath.replace_extension(".keireshader");
    WriteText(generatedSource, hlsl);
    WriteText(manifestPath, compilation.Variants.front().Manifest);
    Keire::AssetImportContext context;
    context.ProjectRoot = directory.Path;
    context.SourceRoot = directory.Path / "Assets";
    context.SourcePath = manifestPath;
    context.RelativePath = manifestPath.lexically_relative(context.SourceRoot);
    context.ReadProjectFile = [root = directory.Path](const std::filesystem::path& relative)
    { return ReadBytes(root / relative); };
    const auto imported = Keire::CreateShaderAssetImporter().ContextualImport(context, ReadBytes(manifestPath));
    CHECK(imported.Diagnostics.empty());
    const auto shader = Keire::ShaderAsset::Decode(imported.Bytes);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::Dxil) != nullptr);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::Msl) != nullptr);

    auto invalidParameter = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter,
                                                         Keire::ShaderGraphValueType::MaterialAttributes);
    invalidParameter.Symbol = "InvalidAttributes";
    graph.Nodes.push_back(std::move(invalidParameter));
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);
}

TEST_CASE("Shader Graph diagnostics identify unused work and validation rejects malformed disconnected nodes")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto unused =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::SimpleNoise, Keire::ShaderGraphValueType::Scalar);
    graph.Nodes.push_back(unused);
    const auto compilation = Keire::CompileShaderGraph(graph);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Statistics.UnusedNodeCount == 1);
    REQUIRE_FALSE(compilation.Diagnostics.empty());
    CHECK(compilation.Diagnostics.front().Severity == Keire::ShaderGraphDiagnosticSeverity::Warning);
    CHECK(compilation.Diagnostics.front().Code == "SG1001");
    CHECK(compilation.Diagnostics.front().Node == unused.Id);

    Pin(graph.Nodes.back(), "Scale").Name = "Malformed Scale";
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);
}

TEST_CASE("Shader Graph stage analysis rejects fragment-only expressions in world-position offset")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto time = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Time);
    graph.Nodes.push_back(time);
    Connect(graph, time, "Seconds", graph.Nodes.front(), "WorldPositionOffset");

    const auto compilation = Keire::CompileShaderGraph(graph);
    CHECK_FALSE(compilation.Succeeded());
    REQUIRE_FALSE(compilation.Diagnostics.empty());
    CHECK(compilation.Diagnostics.back().Severity == Keire::ShaderGraphDiagnosticSeverity::Error);
    CHECK(compilation.Diagnostics.back().Message.find("shader stage") != std::string::npos);
}

TEST_CASE("Shader Graph vertex displacement only declares material uniforms when its expression uses parameters")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto offset =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Vector3);
    offset.Value = Keire::Vector3{0.0F, 0.05F, 0.0F};
    graph.Nodes.push_back(offset);
    Connect(graph, offset, "Value", graph.Nodes.front(), "WorldPositionOffset");

    const auto compilation = Keire::CompileShaderGraph(graph);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Variants.size() == 1);
    CHECK(compilation.Variants.front().Manifest.find("\"usesVertexMaterialParameters\": false") != std::string::npos);
    CHECK(nlohmann::json::parse(compilation.Variants.front().Manifest).at("occlusionSupport") == 0U);
    CHECK(compilation.Variants.front().Hlsl.find("cbuffer VertexMaterialData") == std::string::npos);
}

TEST_CASE("Shader Graph keeps pre-layered PBR master assets source compatible")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    std::erase_if(graph.Nodes.front().Pins,
                  [](const Keire::ShaderGraphPin& pin)
                  {
                      return pin.Name == "Specular" || pin.Name == "ClearCoat" || pin.Name == "ClearCoatRoughness" ||
                             pin.Name == "SheenColor" || pin.Name == "SheenRoughness";
                  });
    const auto compilation = Keire::CompileShaderGraph(graph);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Variants.front().Hlsl.find("const float graphSpecular = saturate(0.5F)") != std::string::npos);
}

TEST_CASE("Sandbox Shader Graph examples decode and compile across the authored progression")
{
    const auto root = std::filesystem::current_path() / "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs";
    constexpr std::array names{std::string_view("01_Foundations/SG_01_StudioPaint.keireshadergraph"),
                               std::string_view("01_Foundations/SG_02_TiledCeramic.keireshadergraph"),
                               std::string_view("01_Foundations/SG_03_NeonPulse.keireshadergraph"),
                               std::string_view("01_Foundations/SG_04_ProceduralCutout.keireshadergraph"),
                               std::string_view("02_Production/SG_05_AutomotiveClearCoat.keireshadergraph"),
                               std::string_view("02_Production/SG_06_BrushedAlloy.keireshadergraph"),
                               std::string_view("02_Production/SG_07_FrostedGlass.keireshadergraph"),
                               std::string_view("02_Production/SG_08_WorldAlignedStone.keireshadergraph"),
                               std::string_view("03_Advanced/SG_09_EnergyDissolve.keireshadergraph"),
                               std::string_view("03_Advanced/SG_10_HologramScanlines.keireshadergraph"),
                               std::string_view("03_Advanced/SG_11_VertexWave.keireshadergraph"),
                               std::string_view("03_Advanced/SG_12_IridescentShield.keireshadergraph")};
    constexpr std::array expectedNodes{4U, 7U, 10U, 7U, 5U, 5U, 5U, 10U, 7U, 11U, 8U, 9U};
    constexpr std::array expectedConnections{3U, 6U, 9U, 6U, 4U, 4U, 4U, 9U, 6U, 12U, 7U, 10U};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        SUBCASE(names[index].data())
        {
            const auto bytes = ReadBytes(root / names[index]);
            const auto graph = Keire::ShaderGraphAsset::DecodeSource(bytes);
            INFO(names[index]);
            CHECK(graph.Nodes.size() == expectedNodes[index]);
            CHECK(graph.Connections.size() == expectedConnections[index]);
            const auto compilation = Keire::CompileShaderGraph(graph);
            const auto diagnostic =
                compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
            INFO(diagnostic);
            REQUIRE(compilation.Succeeded());
            CHECK(compilation.Statistics.UnusedNodeCount == 0);
            CHECK(compilation.Statistics.VariantCount == 1);
        }
    }
}

TEST_CASE("Shader Graph migrates structured schema-v1 assets without changing their topology")
{
    const auto path = std::filesystem::current_path() /
                      "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs/03_Advanced/"
                      "SG_12_IridescentShield.keireshadergraph";
    const auto current = ReadBytes(path);
    auto legacy = nlohmann::json::parse(std::string(reinterpret_cast<const char*>(current.data()), current.size()));
    legacy["schemaVersion"] = 1;
    const auto encoded = legacy.dump();
    const auto graph = Keire::ShaderGraphAsset::DecodeSource(std::as_bytes(std::span(encoded.data(), encoded.size())));
    CHECK(graph.SchemaVersion == Keire::ShaderGraphSourceSchemaVersion);
    CHECK(graph.Nodes.size() == 9);
    CHECK(graph.Connections.size() == 10);
    CHECK_NOTHROW(Keire::ValidateShaderGraph(graph));
    const auto roundTrip = Keire::ShaderGraphAsset::DecodeSource(Keire::ShaderGraphAsset::EncodeSource(graph));
    CHECK(roundTrip == graph);
}

TEST_CASE("Shader Graph lowers texture UV parallax normal detail and emission authoring")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto texture = Parameter("BaseTexture", Keire::ShaderGraphValueType::Texture2D,
                             Keire::AssetId::Parse("ab000000-0000-4000-8000-000000000001"));
    texture.TextureSemantic = Keire::ShaderTextureSemantic::BaseColor;
    auto uv = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::UV, Keire::ShaderGraphValueType::Vector2);
    auto height = Parameter("Height", Keire::ShaderGraphValueType::Scalar, 0.5F);
    auto parallax =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parallax, Keire::ShaderGraphValueType::Vector2);
    auto sample =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::TextureSample, Keire::ShaderGraphValueType::Color);
    auto normal =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::NormalMap, Keire::ShaderGraphValueType::Vector3);
    auto detail =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::DetailNormal, Keire::ShaderGraphValueType::Vector3);
    auto emission = Parameter("Emission", Keire::ShaderGraphValueType::Color, Keire::Color{0.0F, 0.0F, 0.0F, 1.0F});
    graph.Nodes.insert(graph.Nodes.end(), {texture, uv, height, parallax, sample, normal, detail, emission});

    Connect(graph, texture, "Value", sample, "Texture");
    Connect(graph, uv, "UV", parallax, "UV");
    Connect(graph, height, "Value", parallax, "Height");
    Connect(graph, parallax, "UV", sample, "UV");
    Connect(graph, sample, "RGBA", normal, "Sample");
    Connect(graph, normal, "Normal", detail, "Base");
    Connect(graph, detail, "Normal", graph.Nodes.front(), "Normal");
    Connect(graph, sample, "RGBA", graph.Nodes.front(), "BaseColor");
    Connect(graph, emission, "Value", graph.Nodes.front(), "Emission");

    const auto compilation = Keire::CompileShaderGraph(graph);
    const auto diagnostic = compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
    INFO(diagnostic);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Properties.size() == 3);
    const auto& shader = compilation.Variants.front().Hlsl;
    CHECK(shader.find("_KeireMaterial_BaseTexture.Sample") != std::string::npos);
    CHECK(shader.find("DecodeNormal") != std::string::npos);
    CHECK(shader.find("BlendDetailNormal") != std::string::npos);
    CHECK(shader.find("ParallaxUV") != std::string::npos);
    CHECK(shader.find("DistributionGgx") != std::string::npos);
    CHECK(compilation.Variants.front().Manifest.find("\"usesInstancing\": true") != std::string::npos);
    CHECK(compilation.Variants.front().Manifest.find("\"semantic\": \"BaseColor\"") != std::string::npos);
}

TEST_CASE("Shader Graph keyword enumeration is deterministic and bounded")
{
    std::vector<Keire::ShaderGraphKeyword> keywords{{"NORMAL_MAP", {}, "false", true},
                                                    {"QUALITY", {"LOW", "HIGH"}, "HIGH", true}};
    const auto variants = Keire::EnumerateShaderGraphKeywordVariants(keywords);
    const std::vector<std::vector<std::string>> expected{
        {"QUALITY_LOW"}, {"QUALITY_HIGH"}, {"NORMAL_MAP", "QUALITY_LOW"}, {"NORMAL_MAP", "QUALITY_HIGH"}};
    CHECK(variants == expected);
    CHECK_THROWS_AS((void)Keire::EnumerateShaderGraphKeywordVariants(keywords, 3), std::invalid_argument);

    auto graph = Keire::CreateDefaultShaderGraph();
    graph.Keywords = keywords;
    auto keyword =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Keyword, Keire::ShaderGraphValueType::Scalar);
    keyword.Symbol = "NORMAL_MAP";
    graph.Nodes.push_back(keyword);
    graph.Nodes.push_back(Parameter("Tint", Keire::ShaderGraphValueType::Color, Keire::Color{1.0F, 1.0F, 1.0F, 1.0F}));
    const auto compilation = Keire::CompileShaderGraph(graph);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Variants.size() == 4);
    CHECK(compilation.Properties.size() == 1);
    CHECK(std::ranges::all_of(compilation.Variants,
                              [](const auto& variant) { return variant.StableSuffix.size() == 16; }));
    CHECK(compilation.Variants.front().GeneratedSource != compilation.Variants.back().GeneratedSource);
}

TEST_CASE("Shader Graph custom functions confine recursive includes and retain dependencies")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto custom = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Custom, Keire::ShaderGraphValueType::Color);
    custom.Include = "Assets/Shaders/MaterialNodes/Fresnel.hlsli";
    custom.Function = "EvaluateFresnel";
    graph.Nodes.push_back(custom);
    Connect(graph, graph.Nodes.back(), "Result", graph.Nodes.front(), "BaseColor");

    Keire::ShaderGraphCompileOptions options;
    options.ReadInclude = [](const std::filesystem::path& path) -> std::optional<std::string>
    {
        if (path == std::filesystem::path("Assets/Shaders/MaterialNodes/Fresnel.hlsli"))
            return "#include \"Shared.hlsli\"\nfloat4 EvaluateFresnel(float4 value) { return value; }\n";
        if (path == std::filesystem::path("Assets/Shaders/MaterialNodes/Shared.hlsli"))
            return "float ShaderGraphShared(float value) { return value; }\n";
        return std::nullopt;
    };
    const auto compilation = Keire::CompileShaderGraph(graph, options);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Dependencies.size() == 2);
    CHECK(compilation.Variants.front().Hlsl.find("#include \"Shaders/MaterialNodes/Fresnel.hlsli\"") !=
          std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("EvaluateFresnel") != std::string::npos);

    graph.Nodes.back().Include = "../Secrets.hlsli";
    const auto traversal = Keire::CompileShaderGraph(graph, options);
    CHECK_FALSE(traversal.Succeeded());
    REQUIRE_FALSE(traversal.Diagnostics.empty());
    CHECK(traversal.Diagnostics.front().Code == "SG0001");

    graph.Nodes.back().Include = "Assets/Shaders/MaterialNodes/Fresnel.hlsli";
    options.ReadInclude = [](const std::filesystem::path& path) -> std::optional<std::string>
    {
        if (path.filename() == "Fresnel.hlsli")
            return "#include \"Shared.hlsli\"\n";
        if (path.filename() == "Shared.hlsli")
            return "#include \"Fresnel.hlsli\"\n";
        return std::nullopt;
    };
    CHECK_FALSE(Keire::CompileShaderGraph(graph, options).Succeeded());
}

TEST_CASE("Shader Graph validation rejects cycles duplicate inputs and invalid defaults")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto first = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add, Keire::ShaderGraphValueType::Scalar);
    auto second = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add, Keire::ShaderGraphValueType::Scalar);
    graph.Nodes.push_back(first);
    graph.Nodes.push_back(second);
    Connect(graph, first, "Result", second, "A");
    Connect(graph, second, "Result", first, "A");
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);

    graph.Connections.pop_back();
    auto duplicate = graph.Connections.front();
    duplicate.Id = Keire::AssetId::Generate();
    graph.Connections.push_back(duplicate);
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);

    graph.Connections.pop_back();
    Pin(graph.Nodes[1], "A").DefaultValue = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(graph), std::invalid_argument);

    auto orphanKeyword = Keire::CreateDefaultShaderGraph();
    orphanKeyword.Nodes.push_back(
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Keyword, Keire::ShaderGraphValueType::Scalar));
    CHECK_THROWS_AS(Keire::ValidateShaderGraph(orphanKeyword), std::invalid_argument);
}

TEST_CASE("Shader Graph instances merge typed overrides and bake concrete shader variants")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    graph.Keywords = {{"DETAIL", {}, "false", true}, {"QUALITY", {"LOW", "HIGH"}, "HIGH", true}};
    graph.Nodes.push_back(Parameter("Tint", Keire::ShaderGraphValueType::Color, Keire::Color{1.0F, 1.0F, 1.0F, 1.0F}));
    graph.Nodes.push_back(Parameter("Roughness", Keire::ShaderGraphValueType::Scalar, 0.5F));
    graph.Nodes.push_back(Parameter("Metallic", Keire::ShaderGraphValueType::Scalar, 0.65F));

    Keire::ShaderGraphInstanceDefinition root;
    root.Parent = Keire::AssetId::Parse("ab000000-0000-4000-8000-000000000009");
    root.Properties["Tint"] = Keire::Color{0.2F, 0.3F, 0.4F, 1.0F};
    root.KeywordOverrides["QUALITY"] = "LOW";
    Keire::ShaderGraphInstanceDefinition child;
    child.Parent = Keire::AssetId::Parse("ab000000-0000-4000-8000-000000000010");
    child.Properties["Roughness"] = 0.2F;
    child.KeywordOverrides["DETAIL"] = "true";
    const std::array ancestry{root, child};

    const auto resolved = Keire::ResolveShaderGraphInstance(graph, ancestry);
    CHECK(resolved.Properties.size() == 3);
    CHECK(std::get<float>(resolved.Properties.at("Metallic")) == doctest::Approx(0.65F));
    CHECK(resolved.Keywords == std::vector<std::string>{"DETAIL", "QUALITY_LOW"});
    const auto shader = Keire::AssetId::Parse("ab000000-0000-4000-8000-000000000011");
    bool resolverReceivedExpectedKeywords = false;
    const auto baked = Keire::BakeShaderGraphInstance(
        graph, resolved,
        [shader, &resolverReceivedExpectedKeywords](const std::span<const std::string> keywords)
        {
            const std::array expectedKeywords{std::string("DETAIL"), std::string("QUALITY_LOW")};
            resolverReceivedExpectedKeywords = std::ranges::equal(keywords, expectedKeywords);
            return shader;
        });
    CHECK(resolverReceivedExpectedKeywords);
    CHECK(baked.Shader == shader);
    CHECK(baked.Properties == resolved.Properties);

    const auto source = Keire::ShaderGraphInstanceAsset::EncodeSource(child);
    CHECK(Keire::ShaderGraphInstanceAsset::DecodeSource(source) == child);
    const auto cooked = Keire::ShaderGraphInstanceAsset::Encode(child);
    const auto decoded = Keire::ShaderGraphInstanceAsset::Decode(cooked);
    CHECK(decoded->Definition() == child);
    CHECK(Keire::CreateShaderGraphInstanceAssetImporter().Extensions ==
          std::vector<std::string>{".keireshadergraphinstance"});

    child.Properties["Roughness"] = Keire::Color{};
    CHECK_THROWS_AS((void)Keire::ResolveShaderGraphInstance(graph, std::array{root, child}), std::invalid_argument);
}

TEST_CASE("Shader Graph instance import publishes an assignable runtime material")
{
    const auto graphAsset = Keire::AssetId::Parse("ac000000-0000-4000-8000-000000000001");
    const auto rootAsset = Keire::AssetId::Parse("ac000000-0000-4000-8000-000000000002");
    const auto childAsset = Keire::AssetId::Parse("ac000000-0000-4000-8000-000000000003");
    const auto materialAsset = Keire::AssetId::Parse("ac000000-0000-4000-8000-000000000004");
    const auto shaderAsset = Keire::AssetId::Parse("ac000000-0000-4000-8000-000000000005");
    const auto variantOwner = Keire::AssetId::Parse("ac000000-0000-4000-8000-000000000006");

    auto graph = Keire::CreateDefaultShaderGraph();
    graph.GeneratedAssetOwner = variantOwner;
    graph.Nodes.push_back(Parameter("Tint", Keire::ShaderGraphValueType::Color, Keire::Color{1.0F, 1.0F, 1.0F, 1.0F}));
    graph.Nodes.push_back(Parameter("Roughness", Keire::ShaderGraphValueType::Scalar, 0.5F));
    Keire::ShaderGraphInstanceDefinition root;
    root.Parent = graphAsset;
    root.Properties["Tint"] = Keire::Color{0.2F, 0.4F, 0.8F, 1.0F};
    Keire::ShaderGraphInstanceDefinition child;
    child.Parent = rootAsset;
    child.Properties["Roughness"] = 0.18F;

    const auto graphBytes = Keire::ShaderGraphAsset::EncodeSource(graph);
    const auto rootBytes = Keire::ShaderGraphInstanceAsset::EncodeSource(root);
    Keire::AssetImportContext context;
    context.Asset = childAsset;
    context.ProjectRoot = "C:/ShaderGraphInstanceTest";
    context.SourceRoot = context.ProjectRoot / "Assets";
    context.ReadProjectFile = [&](const std::filesystem::path& path)
    {
        const auto normalized = path.lexically_normal().generic_string();
        if (normalized == "Assets/Graphs/Parent.keireshadergraph")
            return std::vector<std::byte>(graphBytes);
        if (normalized == "Assets/Instances/Root.keireshadergraphinstance")
            return std::vector<std::byte>(rootBytes);
        throw std::runtime_error("Unexpected Shader Graph instance test dependency.");
    };
    context.ResolveAssetSource = [&](const Keire::AssetId asset) -> std::optional<Keire::AssetImportSource>
    {
        if (asset == graphAsset)
            return Keire::AssetImportSource{graphAsset, Keire::ShaderGraphAsset::StaticType(),
                                            "Graphs/Parent.keireshadergraph"};
        if (asset == rootAsset)
            return Keire::AssetImportSource{rootAsset, Keire::ShaderGraphInstanceAsset::StaticType(),
                                            "Instances/Root.keireshadergraphinstance"};
        return std::nullopt;
    };
    context.ResolveSubAssetId = [materialAsset](const std::string_view key)
    {
        CHECK(key == "material/default");
        return materialAsset;
    };
    std::string resolvedShaderKey;
    context.ResolveSubAssetIdFor = [&](const Keire::AssetId parent, const std::string_view key)
    {
        CHECK(parent == variantOwner);
        resolvedShaderKey = key;
        return shaderAsset;
    };

    const auto importer = Keire::CreateShaderGraphInstanceAssetImporter();
    CHECK(importer.Version == 3);
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport(context, Keire::ShaderGraphInstanceAsset::EncodeSource(child));
    CHECK(imported.AssetDependencies == std::vector<Keire::AssetId>{graphAsset, rootAsset});
    REQUIRE(imported.SubAssets.size() == 1);
    CHECK(imported.SubAssets.front().Id == materialAsset);
    CHECK(imported.SubAssets.front().Type == Keire::MaterialAsset::StaticType());
    CHECK(resolvedShaderKey.starts_with("shader/"));
    CHECK(std::ranges::find(imported.SubAssets.front().AssetDependencies, shaderAsset) !=
          imported.SubAssets.front().AssetDependencies.end());
    const auto material = Keire::MaterialAsset::Decode(imported.SubAssets.front().Bytes);
    CHECK(material->Definition().Shader == shaderAsset);
    CHECK(std::get<float>(material->Definition().Properties.at("Roughness")) == doctest::Approx(0.18F));
    CHECK(std::get<Keire::Color>(material->Definition().Properties.at("Tint")) == Keire::Color{0.2F, 0.4F, 0.8F, 1.0F});
}
