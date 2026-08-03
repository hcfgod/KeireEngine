#include "Keire/Rendering/MaterialGraph.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

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
    [[nodiscard]] Keire::MaterialGraphPin& Pin(Keire::MaterialGraphNode& node, const std::string_view name)
    {
        const auto found = std::ranges::find(node.Pins, name, &Keire::MaterialGraphPin::Name);
        if (found == node.Pins.end())
            throw std::logic_error("Test Material Graph pin is unavailable.");
        return *found;
    }

    void Connect(Keire::MaterialGraphDefinition& graph, const Keire::MaterialGraphNode& output,
                 const std::string_view outputPin, const Keire::MaterialGraphNode& input,
                 const std::string_view inputPin)
    {
        const auto outputFound = std::ranges::find_if(
            output.Pins, [outputPin](const Keire::MaterialGraphPin& pin)
            { return pin.Name == outputPin && pin.Direction == Keire::MaterialGraphPinDirection::Output; });
        const auto inputFound = std::ranges::find_if(
            input.Pins, [inputPin](const Keire::MaterialGraphPin& pin)
            { return pin.Name == inputPin && pin.Direction == Keire::MaterialGraphPinDirection::Input; });
        REQUIRE(outputFound != output.Pins.end());
        REQUIRE(inputFound != input.Pins.end());
        graph.Connections.push_back(
            {Keire::AssetId::Generate(), {output.Id, outputFound->Id}, {input.Id, inputFound->Id}});
    }

    [[nodiscard]] Keire::MaterialGraphNode
    Parameter(const std::string_view symbol, const Keire::MaterialGraphValueType type, Keire::MaterialGraphValue value)
    {
        auto result = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Parameter, type);
        result.Name = std::string(symbol);
        result.Symbol = symbol;
        result.Value = std::move(value);
        return result;
    }

    struct TemporaryDirectory final
    {
        TemporaryDirectory() : Path(KeireTests::MakeTestDirectory("MaterialGraphShaderImport"))
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
            throw std::runtime_error("Cannot write Material Graph test file.");
    }

    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto bytes = std::as_bytes(std::span(characters));
        return {bytes.begin(), bytes.end()};
    }
} // namespace

TEST_CASE("Material Graph source and cooked assets preserve stable graph identity")
{
    auto definition = Keire::CreateDefaultMaterialGraph();
    auto roughness = Parameter("Roughness", Keire::MaterialGraphValueType::Scalar, 0.42F);
    definition.Nodes.push_back(roughness);
    Connect(definition, definition.Nodes.back(), "Value", definition.Nodes.front(), "Roughness");

    const auto source = Keire::MaterialGraphAsset::EncodeSource(definition);
    const auto sourceDecoded = Keire::MaterialGraphAsset::DecodeSource(source);
    CHECK(sourceDecoded == definition);

    const auto cooked = Keire::MaterialGraphAsset::Encode(definition);
    const auto decoded = Keire::MaterialGraphAsset::Decode(cooked);
    CHECK(decoded->Definition() == definition);
    CHECK(Keire::MaterialGraphAsset::Encode(decoded->Definition()) == cooked);

    const auto importer = Keire::CreateMaterialGraphAssetImporter();
    CHECK(importer.Name == "Keire.MaterialGraph");
    CHECK(importer.Version == 2);
    CHECK(importer.Extensions == std::vector<std::string>{".keirematerialgraph"});
}

TEST_CASE("Material Graph compiles every output model to bounded runtime shader manifests")
{
    constexpr std::array outputs{Keire::MaterialGraphOutput::Surface, Keire::MaterialGraphOutput::Transparent,
                                 Keire::MaterialGraphOutput::Decal, Keire::MaterialGraphOutput::Unlit};
    for (const auto output : outputs)
    {
        const auto graph = Keire::CreateDefaultMaterialGraph(output);
        const auto compilation = Keire::CompileMaterialGraph(graph);
        INFO(static_cast<int>(output));
        REQUIRE(compilation.Succeeded());
        REQUIRE(compilation.Variants.size() == 1);
        CHECK(compilation.Variants.front().Hlsl.find("VSMain") != std::string::npos);
        CHECK(compilation.Variants.front().Hlsl.find("PSMain") != std::string::npos);
        CHECK(compilation.Variants.front().Manifest.find("MaterialGraph-") != std::string::npos);
        if (output == Keire::MaterialGraphOutput::Transparent || output == Keire::MaterialGraphOutput::Decal)
            CHECK(compilation.Variants.front().Manifest.find("\"blend\": true") != std::string::npos);
        else
            CHECK(compilation.Variants.front().Manifest.find("\"blend\": false") != std::string::npos);
    }
}

TEST_CASE("Material Graph generated HLSL compiles through the production shader importer")
{
    const auto repository = std::filesystem::current_path();
#if defined(_WIN32)
    const auto compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe";
#else
    const auto compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler";
#endif
    REQUIRE(std::filesystem::is_regular_file(compiler));

    Keire::MaterialGraphCompileOptions options;
    options.GeneratedSource = "Assets/Generated/MaterialGraphTest.hlsl";
    auto graph = Keire::CreateDefaultMaterialGraph();
    auto texture = Parameter("PreviewTexture", Keire::MaterialGraphValueType::Texture2D, Keire::AssetId{});
    auto sample = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::TextureSample,
                                                 Keire::MaterialGraphValueType::Color);
    auto roughness = Parameter("PreviewRoughness", Keire::MaterialGraphValueType::Scalar, 0.4F);
    auto uv = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::UV, Keire::MaterialGraphValueType::Vector2);
    auto noise = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::SimpleNoise,
                                                Keire::MaterialGraphValueType::Scalar);
    auto modulate =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Multiply, Keire::MaterialGraphValueType::Scalar);
    auto worldNormal = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::WorldNormal,
                                                      Keire::MaterialGraphValueType::Vector3);
    auto fresnel =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Fresnel, Keire::MaterialGraphValueType::Scalar);
    graph.Nodes.insert(graph.Nodes.end(), {texture, sample, roughness, uv, noise, modulate, worldNormal, fresnel});
    Connect(graph, texture, "Value", sample, "Texture");
    Connect(graph, sample, "RGBA", graph.Nodes.front(), "BaseColor");
    Connect(graph, uv, "UV", noise, "UV");
    Connect(graph, noise, "Noise", modulate, "A");
    Connect(graph, roughness, "Value", modulate, "B");
    Connect(graph, modulate, "Result", graph.Nodes.front(), "Roughness");
    Connect(graph, worldNormal, "Vector", fresnel, "Normal");
    Connect(graph, fresnel, "Fresnel", graph.Nodes.front(), "ClearCoat");
    const auto compilation = Keire::CompileMaterialGraph(graph, options);
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
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport(context, ReadBytes(manifest));
    const auto shader = Keire::ShaderAsset::Decode(imported.Bytes);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::Dxil) != nullptr);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
    CHECK(shader->Variant(Keire::ShaderBinaryFormat::Msl) != nullptr);
    CHECK(imported.Diagnostics.empty());
    CHECK(compilation.Variants.front().Hlsl.find("MaterialNoise") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("graphClearCoat") != std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("SurfaceParameters.y > 0.5F") != std::string::npos);
}

TEST_CASE("Material Graph advanced node library lowers modern layered materials and reports cost")
{
    auto graph = Keire::CreateDefaultMaterialGraph();
    auto tint = Parameter("LayerTint", Keire::MaterialGraphValueType::Color, Keire::Color{0.12F, 0.32F, 0.54F, 1.0F});
    auto sheen = Parameter("SheenColor", Keire::MaterialGraphValueType::Color, Keire::Color{0.08F, 0.2F, 0.32F, 1.0F});
    auto uv = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::UV, Keire::MaterialGraphValueType::Vector2);
    auto rotate =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::RotateUV, Keire::MaterialGraphValueType::Vector2);
    auto noise = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::SimpleNoise,
                                                Keire::MaterialGraphValueType::Scalar);
    auto remap =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Remap, Keire::MaterialGraphValueType::Scalar);
    auto desaturate =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Desaturate, Keire::MaterialGraphValueType::Color);
    auto worldNormal = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::WorldNormal,
                                                      Keire::MaterialGraphValueType::Vector3);
    auto fresnel =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Fresnel, Keire::MaterialGraphValueType::Scalar);
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

    const auto compilation = Keire::CompileMaterialGraph(graph);
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

TEST_CASE("Material Graph diagnostics identify unused work and validation rejects malformed disconnected nodes")
{
    auto graph = Keire::CreateDefaultMaterialGraph();
    auto unused = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::SimpleNoise,
                                                 Keire::MaterialGraphValueType::Scalar);
    graph.Nodes.push_back(unused);
    const auto compilation = Keire::CompileMaterialGraph(graph);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Statistics.UnusedNodeCount == 1);
    REQUIRE_FALSE(compilation.Diagnostics.empty());
    CHECK(compilation.Diagnostics.front().Severity == Keire::MaterialGraphDiagnosticSeverity::Warning);
    CHECK(compilation.Diagnostics.front().Code == "MG1001");
    CHECK(compilation.Diagnostics.front().Node == unused.Id);

    Pin(graph.Nodes.back(), "Scale").Name = "Malformed Scale";
    CHECK_THROWS_AS(Keire::ValidateMaterialGraph(graph), std::invalid_argument);
}

TEST_CASE("Material Graph keeps pre-layered PBR master assets source compatible")
{
    auto graph = Keire::CreateDefaultMaterialGraph();
    std::erase_if(graph.Nodes.front().Pins,
                  [](const Keire::MaterialGraphPin& pin)
                  {
                      return pin.Name == "Specular" || pin.Name == "ClearCoat" || pin.Name == "ClearCoatRoughness" ||
                             pin.Name == "SheenColor" || pin.Name == "SheenRoughness";
                  });
    const auto compilation = Keire::CompileMaterialGraph(graph);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Variants.front().Hlsl.find("const float graphSpecular = 0.5") != std::string::npos);
}

TEST_CASE("Sandbox Material Graph examples decode and increase in authored complexity")
{
    const auto root = std::filesystem::current_path() / "Samples/KeireSandbox/Assets/Materials/MaterialGraphs";
    constexpr std::array names{std::string_view("01_BasicPaint.keirematerialgraph"),
                               std::string_view("02_TexturedSurface.keirematerialgraph"),
                               std::string_view("03_ProceduralEmissive.keirematerialgraph"),
                               std::string_view("04_ClearCoatDetail.keirematerialgraph"),
                               std::string_view("05_AdaptiveTechSurface.keirematerialgraph")};
    constexpr std::array expectedNodes{3U, 10U, 10U, 12U, 17U};
    constexpr std::array expectedVariants{1U, 1U, 1U, 1U, 2U};
    std::size_t previousConnections = 0;
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        const auto bytes = ReadBytes(root / names[index]);
        const auto graph = Keire::MaterialGraphAsset::DecodeSource(bytes);
        INFO(names[index]);
        CHECK(graph.Nodes.size() == expectedNodes[index]);
        CHECK(graph.Connections.size() >= previousConnections);
        previousConnections = graph.Connections.size();
        const auto compilation = Keire::CompileMaterialGraph(graph);
        const auto diagnostic =
            compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
        INFO(diagnostic);
        REQUIRE(compilation.Succeeded());
        CHECK(compilation.Statistics.UnusedNodeCount == 0);
        CHECK(compilation.Statistics.VariantCount == expectedVariants[index]);
    }
}

TEST_CASE("Material Graph lowers texture UV parallax normal detail and emission authoring")
{
    auto graph = Keire::CreateDefaultMaterialGraph();
    auto texture = Parameter("BaseTexture", Keire::MaterialGraphValueType::Texture2D,
                             Keire::AssetId::Parse("ab000000-0000-4000-8000-000000000001"));
    texture.TextureSemantic = Keire::ShaderTextureSemantic::BaseColor;
    auto uv = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::UV, Keire::MaterialGraphValueType::Vector2);
    auto height = Parameter("Height", Keire::MaterialGraphValueType::Scalar, 0.5F);
    auto parallax =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Parallax, Keire::MaterialGraphValueType::Vector2);
    auto sample = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::TextureSample,
                                                 Keire::MaterialGraphValueType::Color);
    auto normal =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::NormalMap, Keire::MaterialGraphValueType::Vector3);
    auto detail = Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::DetailNormal,
                                                 Keire::MaterialGraphValueType::Vector3);
    auto emission = Parameter("Emission", Keire::MaterialGraphValueType::Color, Keire::Color{0.0F, 0.0F, 0.0F, 1.0F});
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

    const auto compilation = Keire::CompileMaterialGraph(graph);
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

TEST_CASE("Material Graph keyword enumeration is deterministic and bounded")
{
    std::vector<Keire::MaterialGraphKeyword> keywords{{"NORMAL_MAP", {}, "false", true},
                                                      {"QUALITY", {"LOW", "HIGH"}, "HIGH", true}};
    const auto variants = Keire::EnumerateMaterialGraphKeywordVariants(keywords);
    const std::vector<std::vector<std::string>> expected{
        {"QUALITY_LOW"}, {"QUALITY_HIGH"}, {"NORMAL_MAP", "QUALITY_LOW"}, {"NORMAL_MAP", "QUALITY_HIGH"}};
    CHECK(variants == expected);
    CHECK_THROWS_AS((void)Keire::EnumerateMaterialGraphKeywordVariants(keywords, 3), std::invalid_argument);

    auto graph = Keire::CreateDefaultMaterialGraph();
    graph.Keywords = keywords;
    auto keyword =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Keyword, Keire::MaterialGraphValueType::Scalar);
    keyword.Symbol = "NORMAL_MAP";
    graph.Nodes.push_back(keyword);
    graph.Nodes.push_back(
        Parameter("Tint", Keire::MaterialGraphValueType::Color, Keire::Color{1.0F, 1.0F, 1.0F, 1.0F}));
    const auto compilation = Keire::CompileMaterialGraph(graph);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Variants.size() == 4);
    CHECK(compilation.Properties.size() == 1);
    CHECK(std::ranges::all_of(compilation.Variants,
                              [](const auto& variant) { return variant.StableSuffix.size() == 16; }));
    CHECK(compilation.Variants.front().GeneratedSource != compilation.Variants.back().GeneratedSource);
}

TEST_CASE("Material Graph custom functions confine recursive includes and retain dependencies")
{
    auto graph = Keire::CreateDefaultMaterialGraph();
    auto custom =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Custom, Keire::MaterialGraphValueType::Color);
    custom.Include = "Assets/Shaders/MaterialNodes/Fresnel.hlsli";
    custom.Function = "EvaluateFresnel";
    graph.Nodes.push_back(custom);
    Connect(graph, graph.Nodes.back(), "Result", graph.Nodes.front(), "BaseColor");

    Keire::MaterialGraphCompileOptions options;
    options.ReadInclude = [](const std::filesystem::path& path) -> std::optional<std::string>
    {
        if (path == std::filesystem::path("Assets/Shaders/MaterialNodes/Fresnel.hlsli"))
            return "#include \"Shared.hlsli\"\nfloat4 EvaluateFresnel(float4 value) { return value; }\n";
        if (path == std::filesystem::path("Assets/Shaders/MaterialNodes/Shared.hlsli"))
            return "float MaterialGraphShared(float value) { return value; }\n";
        return std::nullopt;
    };
    const auto compilation = Keire::CompileMaterialGraph(graph, options);
    REQUIRE(compilation.Succeeded());
    CHECK(compilation.Dependencies.size() == 2);
    CHECK(compilation.Variants.front().Hlsl.find("#include \"Assets/Shaders/MaterialNodes/Fresnel.hlsli\"") !=
          std::string::npos);
    CHECK(compilation.Variants.front().Hlsl.find("EvaluateFresnel") != std::string::npos);

    graph.Nodes.back().Include = "../Secrets.hlsli";
    const auto traversal = Keire::CompileMaterialGraph(graph, options);
    CHECK_FALSE(traversal.Succeeded());
    REQUIRE_FALSE(traversal.Diagnostics.empty());
    CHECK(traversal.Diagnostics.front().Code == "MG0001");

    graph.Nodes.back().Include = "Assets/Shaders/MaterialNodes/Fresnel.hlsli";
    options.ReadInclude = [](const std::filesystem::path& path) -> std::optional<std::string>
    {
        if (path.filename() == "Fresnel.hlsli")
            return "#include \"Shared.hlsli\"\n";
        if (path.filename() == "Shared.hlsli")
            return "#include \"Fresnel.hlsli\"\n";
        return std::nullopt;
    };
    CHECK_FALSE(Keire::CompileMaterialGraph(graph, options).Succeeded());
}

TEST_CASE("Material Graph validation rejects cycles duplicate inputs and invalid defaults")
{
    auto graph = Keire::CreateDefaultMaterialGraph();
    auto first =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Add, Keire::MaterialGraphValueType::Scalar);
    auto second =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Add, Keire::MaterialGraphValueType::Scalar);
    graph.Nodes.push_back(first);
    graph.Nodes.push_back(second);
    Connect(graph, first, "Result", second, "A");
    Connect(graph, second, "Result", first, "A");
    CHECK_THROWS_AS(Keire::ValidateMaterialGraph(graph), std::invalid_argument);

    graph.Connections.pop_back();
    auto duplicate = graph.Connections.front();
    duplicate.Id = Keire::AssetId::Generate();
    graph.Connections.push_back(duplicate);
    CHECK_THROWS_AS(Keire::ValidateMaterialGraph(graph), std::invalid_argument);

    graph.Connections.pop_back();
    Pin(graph.Nodes[1], "A").DefaultValue = std::numeric_limits<float>::infinity();
    CHECK_THROWS_AS(Keire::ValidateMaterialGraph(graph), std::invalid_argument);

    auto orphanKeyword = Keire::CreateDefaultMaterialGraph();
    orphanKeyword.Nodes.push_back(
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Keyword, Keire::MaterialGraphValueType::Scalar));
    CHECK_THROWS_AS(Keire::ValidateMaterialGraph(orphanKeyword), std::invalid_argument);
}

TEST_CASE("Material Graph instances merge typed overrides and bake concrete shader variants")
{
    auto graph = Keire::CreateDefaultMaterialGraph();
    graph.Keywords = {{"DETAIL", {}, "false", true}, {"QUALITY", {"LOW", "HIGH"}, "HIGH", true}};
    graph.Nodes.push_back(
        Parameter("Tint", Keire::MaterialGraphValueType::Color, Keire::Color{1.0F, 1.0F, 1.0F, 1.0F}));
    graph.Nodes.push_back(Parameter("Roughness", Keire::MaterialGraphValueType::Scalar, 0.5F));

    Keire::MaterialGraphInstanceDefinition root;
    root.Parent = Keire::AssetId::Parse("ab000000-0000-4000-8000-000000000009");
    root.Properties["Tint"] = Keire::Color{0.2F, 0.3F, 0.4F, 1.0F};
    root.KeywordOverrides["QUALITY"] = "LOW";
    Keire::MaterialGraphInstanceDefinition child;
    child.Parent = Keire::AssetId::Parse("ab000000-0000-4000-8000-000000000010");
    child.Properties["Roughness"] = 0.2F;
    child.KeywordOverrides["DETAIL"] = "true";
    const std::array ancestry{root, child};

    const auto resolved = Keire::ResolveMaterialGraphInstance(graph, ancestry);
    CHECK(resolved.Properties.size() == 2);
    CHECK(resolved.Keywords == std::vector<std::string>{"DETAIL", "QUALITY_LOW"});
    const auto shader = Keire::AssetId::Parse("ab000000-0000-4000-8000-000000000011");
    bool resolverReceivedExpectedKeywords = false;
    const auto baked = Keire::BakeMaterialGraphInstance(
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

    const auto source = Keire::MaterialGraphInstanceAsset::EncodeSource(child);
    CHECK(Keire::MaterialGraphInstanceAsset::DecodeSource(source) == child);
    const auto cooked = Keire::MaterialGraphInstanceAsset::Encode(child);
    const auto decoded = Keire::MaterialGraphInstanceAsset::Decode(cooked);
    CHECK(decoded->Definition() == child);
    CHECK(Keire::CreateMaterialGraphInstanceAssetImporter().Extensions ==
          std::vector<std::string>{".keirematerialinstance"});

    child.Properties["Roughness"] = Keire::Color{};
    CHECK_THROWS_AS((void)Keire::ResolveMaterialGraphInstance(graph, std::array{root, child}), std::invalid_argument);
}
