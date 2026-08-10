#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/MaterialGraphPreview.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        const std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto bytes = std::as_bytes(std::span(characters));
        return {bytes.begin(), bytes.end()};
    }

    void DrainCompilation(KeireEditor::MaterialGraphDocument& document, const double debounceSeconds = 0.075)
    {
        document.AdvanceCompilation(debounceSeconds);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (document.CompilationPending() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
            document.AdvanceCompilation(0.0);
        }
        REQUIRE_FALSE(document.CompilationPending());
    }

} // namespace

TEST_CASE("Material Graph document reuses the stable canvas and preserves last-good preview")
{
    std::size_t previewCount = 0;
    std::size_t liveApplyCount = 0;
    std::size_t persistCount = 0;
    std::vector<std::byte> persisted;
    bool includeAvailable = false;
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Material Graph"});
    KeireEditor::MaterialGraphDocument document(
        {.CompileOptions = {.ReadInclude =
                                [&includeAvailable](const std::filesystem::path& path)
                            {
                                if (includeAvailable &&
                                    path == std::filesystem::path("Assets/Shaders/Nodes/Custom.hlsli"))
                                    return std::optional<std::string>(
                                        "float4 EvaluateCustomMaterialNode(float4 value) { return value; }\n");
                                return std::optional<std::string>{};
                            }},
         .Preview =
             [&previewCount](Keire::AssetId, const Keire::MaterialGraphCompilation& compilation,
                             const KeireEditor::MaterialGraphPreviewSettings& settings)
         {
             CHECK(compilation.Succeeded());
             const bool supportedPreview = settings.Mesh == Keire::MaterialGraphPreviewMesh::Sphere ||
                                           settings.Mesh == Keire::MaterialGraphPreviewMesh::Cube;
             CHECK(supportedPreview);
             ++previewCount;
         },
         .LiveApply =
             [&liveApplyCount](Keire::AssetId, const Keire::MaterialGraphDefinition& definition,
                               const Keire::MaterialGraphCompilation& compilation,
                               std::span<const Keire::Ref<Keire::ShaderAsset>> developmentShaders)
         {
             CHECK_FALSE(definition.Nodes.empty());
             CHECK(compilation.Succeeded());
             CHECK(developmentShaders.size() <= compilation.Variants.size());
             ++liveApplyCount;
         },
         .Persist =
             [&persistCount, &persisted](Keire::AssetId, const std::span<const std::byte> bytes)
         {
             ++persistCount;
             persisted.assign(bytes.begin(), bytes.end());
         }});

    const auto asset = Keire::AssetId::Generate();
    document.Create(asset, Keire::CreateDefaultMaterialGraph(), undo);
    REQUIRE(document.Publishable());
    REQUIRE(document.LastGoodCompilation());
    REQUIRE(document.LastGoodDefinition());
    CHECK(previewCount == 1);
    CHECK(liveApplyCount == 1);
    const auto initialPreviewDefinition = *document.LastGoodDefinition();
    const auto canvas = document.BuildCanvasModel();
    REQUIRE(canvas.Nodes.size() == 1);
    CHECK(canvas.Nodes.front().Label == "PBR Master");
    CHECK(canvas.Nodes.front().Pins.size() == document.Definition().Nodes.front().Pins.size());
    CHECK(canvas.Node(canvas.Nodes.front().Id) == document.Definition().Nodes.front().Id);
    const auto masterId = document.Definition().Nodes.front().Id;
    REQUIRE(document.MoveNode(masterId, {420.0F, 180.0F}));
    CHECK(document.Definition().Nodes.front().EditorPosition == Keire::Vector2{420.0F, 180.0F});
    CHECK_FALSE(document.CompilationPending());
    CHECK(previewCount == 1);
    CHECK(liveApplyCount == 1);
    REQUIRE(document.Undo());
    CHECK_FALSE(document.CompilationPending());
    CHECK(previewCount == 1);
    CHECK(liveApplyCount == 1);
    REQUIRE(document.Redo());
    CHECK_FALSE(document.CompilationPending());
    CHECK(previewCount == 1);
    CHECK(liveApplyCount == 1);

    auto custom =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Custom, Keire::MaterialGraphValueType::Color);
    custom.Include = "Assets/Shaders/Nodes/Custom.hlsli";
    REQUIRE(document.AddNode(custom));
    CHECK(document.CompilationPending());
    document.AdvanceCompilation(0.07);
    CHECK(document.CompilationPending());
    DrainCompilation(document, 0.005);
    CHECK_FALSE(document.Publishable());
    REQUIRE(document.LastGoodCompilation());
    REQUIRE(document.LastGoodDefinition());
    CHECK(*document.LastGoodDefinition() == initialPreviewDefinition);
    CHECK(previewCount == 1);
    CHECK(liveApplyCount == 1);
    CHECK_THROWS_AS(document.Save(), std::logic_error);

    includeAvailable = true;
    REQUIRE(document.EditNode(custom.Id, [](Keire::MaterialGraphNode& node) { node.Name = "Safe Custom Node"; }));
    DrainCompilation(document);
    INFO(document.Diagnostic());
    REQUIRE(document.Publishable());
    CHECK(previewCount == 2);
    CHECK(liveApplyCount == 2);
    REQUIRE(document.Undo());
    DrainCompilation(document);
    CHECK(previewCount == 3);
    CHECK(liveApplyCount == 3);
    REQUIRE(document.Redo());
    DrainCompilation(document);
    CHECK(previewCount == 4);
    CHECK(liveApplyCount == 4);
    document.SetPreviewSettings({.Mesh = Keire::MaterialGraphPreviewMesh::Cube,
                                 .Exposure = 1.4F,
                                 .EnvironmentIntensity = 0.8F,
                                 .RotationDegrees = -25.0F});
    CHECK(previewCount == 5);
    CHECK(liveApplyCount == 4);
    CHECK_THROWS_AS(document.SetPreviewSettings({.Exposure = 0.0F}), std::invalid_argument);
    const auto savedDefinition = document.Definition();
    document.Save();
    CHECK(persistCount == 1);
    CHECK_FALSE(persisted.empty());
    CHECK(Keire::MaterialGraphAsset::DecodeSource(persisted) == savedDefinition);
    CHECK_FALSE(document.Dirty());

    document.Close();
    document.Open(asset, persisted, 2, undo);
    CHECK(document.Definition() == savedDefinition);
    CHECK_FALSE(document.Dirty());
}

TEST_CASE("Material Graph live apply publishes parameters immediately and compiles only when runtime code changes")
{
    std::vector<std::size_t> publishedShaderCounts;
    std::vector<float> publishedRoughness;
    KeireEditor::MaterialGraphDocument document(
        {.LiveApply =
             [&publishedShaderCounts,
              &publishedRoughness](Keire::AssetId, const Keire::MaterialGraphDefinition&,
                                   const Keire::MaterialGraphCompilation& compilation,
                                   const std::span<const Keire::Ref<Keire::ShaderAsset>> shaders)
         {
             publishedShaderCounts.push_back(shaders.size());
             const auto roughness = std::ranges::find(compilation.Properties, std::string("LiveRoughness"),
                                                      &Keire::ShaderPropertyDefinition::Name);
             if (roughness != compilation.Properties.end())
                 publishedRoughness.push_back(roughness->DefaultValue.X);
         },
         .Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    document.Create(Keire::AssetId::Generate());
    REQUIRE(publishedShaderCounts == std::vector<std::size_t>{0});

    auto roughness =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Parameter, Keire::MaterialGraphValueType::Scalar);
    roughness.Name = "Live Roughness";
    roughness.Symbol = "LiveRoughness";
    roughness.Value = 0.2F;
    REQUIRE(document.AddNode(roughness));
    const auto& master = document.Definition().Nodes.front();
    const auto roughnessInput =
        std::ranges::find(master.Pins, std::string("Roughness"), &Keire::MaterialGraphPin::Name);
    REQUIRE(roughnessInput != master.Pins.end());
    const auto masterId = master.Id;
    const auto roughnessInputId = roughnessInput->Id;
    const auto baseColorInput =
        std::ranges::find(master.Pins, std::string("BaseColor"), &Keire::MaterialGraphPin::Name);
    REQUIRE(baseColorInput != master.Pins.end());
    const auto baseColorPin = baseColorInput->Id;
    REQUIRE(document.AddConnection({{}, {roughness.Id, roughness.Pins.front().Id}, {masterId, roughnessInputId}}));
    DrainCompilation(document);
    REQUIRE(publishedShaderCounts.back() == 1);
    REQUIRE(publishedRoughness.back() == doctest::Approx(0.2F));

    const auto publicationsBeforeParameters = publishedShaderCounts.size();
    REQUIRE(document.EditNode(roughness.Id, [](Keire::MaterialGraphNode& node) { node.Value = 0.45F; }));
    CHECK_FALSE(document.CompilationPending());
    REQUIRE(publishedShaderCounts.size() == publicationsBeforeParameters + 1);
    CHECK(publishedShaderCounts.back() == 0);
    CHECK(publishedRoughness.back() == doctest::Approx(0.45F));
    REQUIRE(document.EditNode(roughness.Id, [](Keire::MaterialGraphNode& node) { node.Value = 0.8F; }));
    CHECK_FALSE(document.CompilationPending());
    REQUIRE(publishedShaderCounts.size() == publicationsBeforeParameters + 2);
    CHECK(publishedShaderCounts.back() == 0);
    CHECK(publishedRoughness.back() == doctest::Approx(0.8F));

    document.ApplyLiveRevision();
    REQUIRE(publishedShaderCounts.size() == publicationsBeforeParameters + 3);
    CHECK(publishedShaderCounts.back() == 1);
    CHECK(publishedRoughness.back() == doctest::Approx(0.8F));

    REQUIRE(document.EditNode(masterId,
                              [baseColorPin](Keire::MaterialGraphNode& node)
                              {
                                  const auto pin =
                                      std::ranges::find(node.Pins, baseColorPin, &Keire::MaterialGraphPin::Id);
                                  REQUIRE(pin != node.Pins.end());
                                  pin->DefaultValue = Keire::Color{0.1F, 0.25F, 0.8F, 1.0F};
                              }));
    DrainCompilation(document);
    CHECK(publishedShaderCounts.back() == 1);
}

TEST_CASE("Material Graph document validates interactive cables and replacement warnings")
{
    KeireEditor::MaterialGraphDocument document({.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    document.Create(Keire::AssetId::Generate());
    auto first =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Constant, Keire::MaterialGraphValueType::Color);
    first.Value = Keire::Color{1.0F, 0.0F, 0.0F, 1.0F};
    auto second = first;
    second.Id = Keire::AssetId::Generate();
    for (auto& pin : second.Pins)
        pin.Id = Keire::AssetId::Generate();
    second.Value = Keire::Color{0.0F, 1.0F, 0.0F, 1.0F};
    REQUIRE(document.AddNode(first));
    REQUIRE(document.AddNode(second));

    const auto outputPin = first.Pins.front().Id;
    const auto secondOutputPin = second.Pins.front().Id;
    const auto& master = document.Definition().Nodes.front();
    const auto baseColor = std::ranges::find(master.Pins, std::string("BaseColor"), &Keire::MaterialGraphPin::Name);
    REQUIRE(baseColor != master.Pins.end());
    const Keire::MaterialGraphEndpoint input{master.Id, baseColor->Id};
    CHECK(document.CheckConnection({first.Id, outputPin}, input).Status ==
          KeireEditor::NodeGraphConnectionValidationStatus::Accept);
    REQUIRE(document.AddConnection({{}, {first.Id, outputPin}, input}));
    const auto replacement = document.CheckConnection({second.Id, secondOutputPin}, input);
    CHECK(replacement.Status == KeireEditor::NodeGraphConnectionValidationStatus::AcceptWithWarning);
    CHECK_FALSE(replacement.Diagnostic.empty());
    REQUIRE(document.AddConnection({{}, {second.Id, secondOutputPin}, input}));
    CHECK(document.Definition().Connections.size() == 1);

    const auto canvas = document.BuildCanvasModel();
    REQUIRE(canvas.Connections.size() == 1);
    CHECK(canvas.Connection(canvas.Connections.front().Id) == document.Definition().Connections.front().Id);
}

TEST_CASE("Material Graph live preview renders every built-in shape and custom meshes")
{
    const std::array properties{
        Keire::ShaderPropertyDefinition{.Name = "BaseColor",
                                        .Type = Keire::ShaderPropertyType::Color,
                                        .DefaultValue = {0.72F, 0.18F, 0.08F, 0.78F}},
        Keire::ShaderPropertyDefinition{
            .Name = "Metallic", .Type = Keire::ShaderPropertyType::Scalar, .DefaultValue = {0.65F, 0.0F, 0.0F, 0.0F}},
        Keire::ShaderPropertyDefinition{
            .Name = "Roughness", .Type = Keire::ShaderPropertyType::Scalar, .DefaultValue = {0.24F, 0.0F, 0.0F, 0.0F}},
    };
    KeireEditor::MaterialGraphPreviewRequest request{
        .Output = Keire::MaterialGraphOutput::Transparent,
        .Properties = properties,
        .Width = 96,
        .Height = 72,
        .Exposure = 1.35F,
        .EnvironmentIntensity = 1.6F,
        .RotationDegrees = -18.0F,
    };
    const auto sphere = KeireEditor::RenderMaterialGraphPreview(request);
    request.Mesh = Keire::MaterialGraphPreviewMesh::Plane;
    const auto plane = KeireEditor::RenderMaterialGraphPreview(request);
    request.Mesh = Keire::MaterialGraphPreviewMesh::Cube;
    const auto cube = KeireEditor::RenderMaterialGraphPreview(request);
    request.Mesh = Keire::MaterialGraphPreviewMesh::Custom;
    request.CustomMesh = Keire::MeshAsset::Cube();
    const auto custom = KeireEditor::RenderMaterialGraphPreview(request);

    CHECK(sphere.size() == 96U * 72U * 4U);
    CHECK(plane.size() == sphere.size());
    CHECK(cube.size() == sphere.size());
    CHECK(sphere != plane);
    CHECK(plane != cube);
    CHECK(custom == cube);

    request.CustomMesh.Reset();
    CHECK_THROWS_AS((void)KeireEditor::RenderMaterialGraphPreview(request), std::invalid_argument);
    request.Mesh = Keire::MaterialGraphPreviewMesh::Sphere;
    request.Width = 16;
    CHECK_THROWS_AS((void)KeireEditor::RenderMaterialGraphPreview(request), std::invalid_argument);
    request.Width = 96;
    request.Exposure = 0.0F;
    CHECK_THROWS_AS((void)KeireEditor::RenderMaterialGraphPreview(request), std::invalid_argument);
    request.Exposure = 1.0F;
    request.CancellationRequested = [] { return true; };
    CHECK_THROWS_AS((void)KeireEditor::RenderMaterialGraphPreview(request), std::runtime_error);
}

TEST_CASE("Material Graph live preview evaluates procedural nodes instead of property-name approximations")
{
    const auto source = std::filesystem::current_path() /
                        "Samples/KeireSandbox/Assets/Materials/MaterialGraphs/03_ProceduralEmissive.keirematerialgraph";
    const auto graph = Keire::MaterialGraphAsset::DecodeSource(ReadBytes(source));
    const auto compilation = Keire::CompileMaterialGraph(graph);
    REQUIRE(compilation.Succeeded());

    KeireEditor::MaterialGraphPreviewRequest request{
        .Output = graph.Output,
        .Mesh = Keire::MaterialGraphPreviewMesh::Plane,
        .Definition = &graph,
        .Properties = compilation.Properties,
        .Width = 128,
        .Height = 96,
        .Exposure = 1.0F,
        .EnvironmentIntensity = 1.0F,
        .RotationDegrees = 0.0F,
    };
    const auto evaluated = KeireEditor::RenderMaterialGraphPreview(request);
    CHECK(KeireEditor::RenderMaterialGraphPreview(request) == evaluated);

    request.Definition = nullptr;
    const auto propertyApproximation = KeireEditor::RenderMaterialGraphPreview(request);
    CHECK(evaluated != propertyApproximation);
    std::uint64_t evaluatedBlue = 0;
    std::uint64_t approximatedBlue = 0;
    for (std::size_t index = 2; index < evaluated.size(); index += 4)
    {
        evaluatedBlue += std::to_integer<std::uint8_t>(evaluated[index]);
        approximatedBlue += std::to_integer<std::uint8_t>(propertyApproximation[index]);
    }
    CHECK(evaluatedBlue > approximatedBlue);
}

TEST_CASE("Material Graph Sandbox progression compiles without dead authored work")
{
    const auto root = std::filesystem::current_path() / "Samples/KeireSandbox/Assets/Materials/MaterialGraphs";
    constexpr std::array names{std::string_view("01_BasicPaint.keirematerialgraph"),
                               std::string_view("02_TexturedSurface.keirematerialgraph"),
                               std::string_view("03_ProceduralEmissive.keirematerialgraph"),
                               std::string_view("04_ClearCoatDetail.keirematerialgraph"),
                               std::string_view("05_AdaptiveTechSurface.keirematerialgraph")};
    constexpr std::array expectedNodes{3U, 10U, 10U, 12U, 17U};
    constexpr std::array expectedVariants{1U, 1U, 1U, 1U, 2U};
    KeireEditor::MaterialGraphDocument document({.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        const auto bytes = ReadBytes(root / names[index]);
        const auto graph = Keire::MaterialGraphAsset::DecodeSource(bytes);
        INFO(names[index]);
        CHECK(graph.Nodes.size() == expectedNodes[index]);
        const auto compilation = Keire::CompileMaterialGraph(graph);
        const auto diagnostic =
            compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
        INFO(diagnostic);
        REQUIRE(compilation.Succeeded());
        CHECK(compilation.Statistics.UnusedNodeCount == 0);
        CHECK(compilation.Statistics.VariantCount == expectedVariants[index]);
        document.Open(Keire::AssetId::Generate(), bytes, index + 1U, {});
        const auto canvas = document.BuildCanvasModel();
        CHECK(canvas.Nodes.size() == graph.Nodes.size());
        CHECK(canvas.Connections.size() == graph.Connections.size());
        document.Close();
    }
}

TEST_CASE("Material Graph hero example imports compiled shader and runtime material subassets")
{
    const auto path = std::filesystem::current_path() /
                      "Samples/KeireSandbox/Assets/Materials/MaterialGraphs/05_AdaptiveTechSurface.keirematerialgraph";
    const auto bytes = ReadBytes(path);
    Keire::AssetImportContext context;
    context.Asset = Keire::AssetId::Generate();
    context.ProjectRoot = std::filesystem::current_path();
    context.SourceRoot = context.ProjectRoot / "Samples/KeireSandbox/Assets";
    context.SourcePath = path;
    context.RelativePath = path.lexically_relative(context.SourceRoot);
    context.ReadProjectFile = [root = context.ProjectRoot](const std::filesystem::path& relative)
    { return ReadBytes(root / relative); };
    std::map<std::string, Keire::AssetId, std::less<>> generatedIds;
    context.ResolveSubAssetId = [&generatedIds](const std::string_view key)
    { return generatedIds.try_emplace(std::string(key), Keire::AssetId::Generate()).first->second; };
    const auto importer = Keire::CreateMaterialGraphAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport(context, bytes);
    REQUIRE(imported.SubAssets.size() == 3);

    std::size_t shaders = 0;
    const Keire::AssetGeneratedSubAsset* runtimeMaterial = nullptr;
    for (const auto& subAsset : imported.SubAssets)
    {
        if (subAsset.Type == Keire::ShaderAsset::StaticType())
        {
            ++shaders;
            const auto shader = Keire::ShaderAsset::Decode(subAsset.Bytes);
            REQUIRE(shader);
            CHECK(shader->Variant(Keire::ShaderBinaryFormat::Dxil) != nullptr);
            CHECK(shader->Variant(Keire::ShaderBinaryFormat::SpirV) != nullptr);
            CHECK(shader->Variant(Keire::ShaderBinaryFormat::Msl) != nullptr);
        }
        else if (subAsset.Type == Keire::MaterialAsset::StaticType())
            runtimeMaterial = &subAsset;
    }
    CHECK(shaders == 2);
    REQUIRE(runtimeMaterial != nullptr);
    const auto material = Keire::MaterialAsset::Decode(runtimeMaterial->Bytes);
    REQUIRE(material);
    CHECK(material->Definition().Shader);
    CHECK(std::ranges::find(runtimeMaterial->AssetDependencies, material->Definition().Shader) !=
          runtimeMaterial->AssetDependencies.end());
}
