#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/ShaderGraphPreview.h"

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

    void DrainCompilation(KeireEditor::ShaderGraphDocument& document, const double debounceSeconds = 0.075)
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

TEST_CASE("Shader Graph document preserves reusable function metadata without standalone shader publication")
{
    std::vector<std::byte> persisted;
    KeireEditor::ShaderGraphDocument document(
        {.Persist = [&persisted](Keire::AssetId, const std::span<const std::byte> bytes)
         { persisted.assign(bytes.begin(), bytes.end()); }});
    auto function = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::MaterialFunction);
    function.Description = "Shared production color transform.";
    function.Category = "Project / Surface";
    function.SortPriority = 42;
    function.ExposeToLibrary = false;
    const auto input =
        std::ranges::find(function.Body.Nodes, Keire::ShaderGraphNodeKind::Parameter, &Keire::ShaderGraphNode::Kind);
    REQUIRE(input != function.Body.Nodes.end());
    const auto inputId = input->Id;

    document.Open(Keire::AssetId::Generate(), function, 1);
    CHECK(document.ReusableGraph());
    CHECK(document.Publishable());
    CHECK(document.Compilation().Variants.empty());
    REQUIRE(document.EditNode(inputId, [](auto& node) { node.Name = "Source Color"; }));
    CHECK(document.Dirty());
    document.Save();
    CHECK_FALSE(document.Dirty());
    REQUIRE_FALSE(persisted.empty());

    const auto decoded = Keire::MaterialFunctionAsset::DecodeSource(persisted);
    CHECK(decoded.Description == function.Description);
    CHECK(decoded.Category == function.Category);
    CHECK(decoded.SortPriority == function.SortPriority);
    CHECK(decoded.ExposeToLibrary == function.ExposeToLibrary);
    const auto decodedInput = std::ranges::find(decoded.Body.Nodes, inputId, &Keire::ShaderGraphNode::Id);
    REQUIRE(decodedInput != decoded.Body.Nodes.end());
    CHECK(decodedInput->Name == "Source Color");
}

TEST_CASE("Shader Graph document reuses the stable canvas and preserves last-good preview")
{
    std::size_t previewCount = 0;
    std::size_t liveApplyCount = 0;
    std::size_t persistCount = 0;
    std::vector<std::byte> persisted;
    bool includeAvailable = false;
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Shader Graph"});
    KeireEditor::ShaderGraphDocument document(
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
             [&previewCount](Keire::AssetId, const Keire::ShaderGraphCompilation& compilation,
                             const KeireEditor::ShaderGraphPreviewSettings& settings)
         {
             CHECK(compilation.Succeeded());
             const bool supportedPreview = settings.Mesh == Keire::ShaderGraphPreviewMesh::Sphere ||
                                           settings.Mesh == Keire::ShaderGraphPreviewMesh::Cube;
             CHECK(supportedPreview);
             ++previewCount;
         },
         .LiveApply =
             [&liveApplyCount](Keire::AssetId, const Keire::ShaderGraphDefinition& definition,
                               const Keire::ShaderGraphCompilation& compilation,
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
    document.Create(asset, Keire::CreateDefaultShaderGraph(), undo);
    REQUIRE(document.Publishable());
    REQUIRE(document.LastGoodCompilation());
    REQUIRE(document.LastGoodDefinition());
    CHECK(previewCount == 1);
    CHECK(liveApplyCount == 1);
    const auto initialPreviewDefinition = *document.LastGoodDefinition();
    const auto canvas = document.BuildCanvasModel();
    REQUIRE(canvas.Nodes.size() == 1);
    CHECK(canvas.Nodes.front().Label == "Lit Shader Output");
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

    auto custom = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Custom, Keire::ShaderGraphValueType::Color);
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
    REQUIRE(document.EditNode(custom.Id, [](Keire::ShaderGraphNode& node) { node.Name = "Safe Custom Node"; }));
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
    document.SetPreviewSettings({.Mesh = Keire::ShaderGraphPreviewMesh::Cube,
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
    CHECK(Keire::ShaderGraphAsset::DecodeSource(persisted) == savedDefinition);
    CHECK_FALSE(document.Dirty());

    document.Close();
    document.Open(asset, persisted, 2, undo);
    CHECK(document.Definition() == savedDefinition);
    CHECK_FALSE(document.Dirty());
}

TEST_CASE("Shader Graph live apply publishes parameters immediately and compiles only when runtime code changes")
{
    std::vector<std::size_t> publishedShaderCounts;
    std::vector<float> publishedRoughness;
    KeireEditor::ShaderGraphDocument document(
        {.LiveApply =
             [&publishedShaderCounts, &publishedRoughness](
                 Keire::AssetId, const Keire::ShaderGraphDefinition&, const Keire::ShaderGraphCompilation& compilation,
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
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    roughness.Name = "Live Roughness";
    roughness.Symbol = "LiveRoughness";
    roughness.Value = 0.2F;
    REQUIRE(document.AddNode(roughness));
    const auto& master = document.Definition().Nodes.front();
    const auto roughnessInput = std::ranges::find(master.Pins, std::string("Roughness"), &Keire::ShaderGraphPin::Name);
    REQUIRE(roughnessInput != master.Pins.end());
    const auto masterId = master.Id;
    const auto roughnessInputId = roughnessInput->Id;
    const auto baseColorInput = std::ranges::find(master.Pins, std::string("BaseColor"), &Keire::ShaderGraphPin::Name);
    REQUIRE(baseColorInput != master.Pins.end());
    const auto baseColorPin = baseColorInput->Id;
    REQUIRE(document.AddConnection({{}, {roughness.Id, roughness.Pins.front().Id}, {masterId, roughnessInputId}}));
    DrainCompilation(document);
    REQUIRE(publishedShaderCounts.back() == 1);
    REQUIRE(publishedRoughness.back() == doctest::Approx(0.2F));

    const auto publicationsBeforeParameters = publishedShaderCounts.size();
    REQUIRE(document.EditNode(roughness.Id, [](Keire::ShaderGraphNode& node) { node.Value = 0.45F; }));
    CHECK_FALSE(document.CompilationPending());
    REQUIRE(publishedShaderCounts.size() == publicationsBeforeParameters + 1);
    CHECK(publishedShaderCounts.back() == 0);
    CHECK(publishedRoughness.back() == doctest::Approx(0.45F));
    REQUIRE(document.EditNode(roughness.Id, [](Keire::ShaderGraphNode& node) { node.Value = 0.8F; }));
    CHECK_FALSE(document.CompilationPending());
    REQUIRE(publishedShaderCounts.size() == publicationsBeforeParameters + 2);
    CHECK(publishedShaderCounts.back() == 0);
    CHECK(publishedRoughness.back() == doctest::Approx(0.8F));

    document.ApplyLiveRevision();
    REQUIRE(publishedShaderCounts.size() == publicationsBeforeParameters + 3);
    CHECK(publishedShaderCounts.back() == 1);
    CHECK(publishedRoughness.back() == doctest::Approx(0.8F));

    REQUIRE(document.EditNode(masterId,
                              [baseColorPin](Keire::ShaderGraphNode& node)
                              {
                                  const auto pin =
                                      std::ranges::find(node.Pins, baseColorPin, &Keire::ShaderGraphPin::Id);
                                  REQUIRE(pin != node.Pins.end());
                                  pin->DefaultValue = Keire::Color{0.1F, 0.25F, 0.8F, 1.0F};
                              }));
    DrainCompilation(document);
    CHECK(publishedShaderCounts.back() == 1);
}

TEST_CASE("Shader Graph document validates interactive cables and replacement warnings")
{
    KeireEditor::ShaderGraphDocument document({.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    document.Create(Keire::AssetId::Generate());
    auto first = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Color);
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
    const auto baseColor = std::ranges::find(master.Pins, std::string("BaseColor"), &Keire::ShaderGraphPin::Name);
    REQUIRE(baseColor != master.Pins.end());
    const Keire::ShaderGraphEndpoint input{master.Id, baseColor->Id};
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

TEST_CASE("Shader Graph live preview renders every built-in shape and custom meshes")
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
    KeireEditor::ShaderGraphPreviewRequest request{
        .Output = Keire::ShaderGraphOutput::Transparent,
        .Properties = properties,
        .Width = 96,
        .Height = 72,
        .Exposure = 1.35F,
        .EnvironmentIntensity = 1.6F,
        .RotationDegrees = -18.0F,
    };
    const auto sphere = KeireEditor::RenderShaderGraphPreview(request);
    request.Mesh = Keire::ShaderGraphPreviewMesh::Plane;
    const auto plane = KeireEditor::RenderShaderGraphPreview(request);
    request.Mesh = Keire::ShaderGraphPreviewMesh::Cube;
    const auto cube = KeireEditor::RenderShaderGraphPreview(request);
    request.Mesh = Keire::ShaderGraphPreviewMesh::Custom;
    request.CustomMesh = Keire::MeshAsset::Cube();
    const auto custom = KeireEditor::RenderShaderGraphPreview(request);

    CHECK(sphere.size() == 96U * 72U * 4U);
    CHECK(plane.size() == sphere.size());
    CHECK(cube.size() == sphere.size());
    CHECK(sphere != plane);
    CHECK(plane != cube);
    CHECK(custom == cube);

    request.CustomMesh.Reset();
    CHECK_THROWS_AS((void)KeireEditor::RenderShaderGraphPreview(request), std::invalid_argument);
    request.Mesh = Keire::ShaderGraphPreviewMesh::Sphere;
    request.Width = 16;
    CHECK_THROWS_AS((void)KeireEditor::RenderShaderGraphPreview(request), std::invalid_argument);
    request.Width = 96;
    request.Exposure = 0.0F;
    CHECK_THROWS_AS((void)KeireEditor::RenderShaderGraphPreview(request), std::invalid_argument);
    request.Exposure = 1.0F;
    request.CancellationRequested = [] { return true; };
    CHECK_THROWS_AS((void)KeireEditor::RenderShaderGraphPreview(request), std::runtime_error);
}

TEST_CASE("Shader Graph live preview evaluates procedural nodes instead of property-name approximations")
{
    const auto source = std::filesystem::current_path() /
                        "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs/01_Foundations/"
                        "SG_03_NeonPulse.keireshadergraph";
    const auto graph = Keire::ShaderGraphAsset::DecodeSource(ReadBytes(source));
    const auto compilation = Keire::CompileShaderGraph(graph);
    REQUIRE(compilation.Succeeded());

    KeireEditor::ShaderGraphPreviewRequest request{
        .Output = graph.Output,
        .Mesh = Keire::ShaderGraphPreviewMesh::Plane,
        .Definition = &graph,
        .Properties = compilation.Properties,
        .Width = 128,
        .Height = 96,
        .Exposure = 1.0F,
        .EnvironmentIntensity = 1.0F,
        .RotationDegrees = 0.0F,
    };
    const auto evaluated = KeireEditor::RenderShaderGraphPreview(request);
    CHECK(KeireEditor::RenderShaderGraphPreview(request) == evaluated);

    request.Definition = nullptr;
    const auto propertyApproximation = KeireEditor::RenderShaderGraphPreview(request);
    CHECK(evaluated != propertyApproximation);
    std::uint64_t evaluatedBlue = 0;
    std::uint64_t approximatedBlue = 0;
    for (std::size_t index = 2; index < evaluated.size(); index += 4)
    {
        evaluatedBlue += std::to_integer<std::uint8_t>(evaluated[index]);
        approximatedBlue += std::to_integer<std::uint8_t>(propertyApproximation[index]);
    }
    CHECK(evaluatedBlue != approximatedBlue);
}

TEST_CASE("Shader Graph live preview samples supplied material textures")
{
    const auto source = std::filesystem::current_path() /
                        "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs/01_Foundations/"
                        "SG_02_TiledCeramic.keireshadergraph";
    const auto graph = Keire::ShaderGraphAsset::DecodeSource(ReadBytes(source));
    const auto compilation = Keire::CompileShaderGraph(graph);
    REQUIRE(compilation.Succeeded());
    const auto textureParameter =
        std::ranges::find_if(graph.Nodes,
                             [](const Keire::ShaderGraphNode& node)
                             {
                                 return node.Kind == Keire::ShaderGraphNodeKind::Parameter &&
                                        node.ValueType == Keire::ShaderGraphValueType::Texture2D;
                             });
    REQUIRE(textureParameter != graph.Nodes.end());
    const auto textureAsset = std::get<Keire::AssetId>(textureParameter->Value);
    REQUIRE(textureAsset);

    Keire::TextureMipLevel mip;
    mip.Width = 2;
    mip.Height = 2;
    mip.Pixels = {
        std::byte{255}, std::byte{16},  std::byte{8},   std::byte{255}, std::byte{255}, std::byte{16},
        std::byte{8},   std::byte{255}, std::byte{255}, std::byte{16},  std::byte{8},   std::byte{255},
        std::byte{255}, std::byte{16},  std::byte{8},   std::byte{255},
    };
    Keire::TextureImportSettings textureSettings;
    textureSettings.Mips = Keire::TextureMipPolicy::None;
    const auto texture = Keire::CreateRef<Keire::Texture2DAsset>(textureSettings, std::vector{std::move(mip)});
    const std::array textures{KeireEditor::ShaderGraphPreviewTexture{textureAsset, texture}};
    KeireEditor::ShaderGraphPreviewRequest request{
        .Output = graph.Output,
        .Mesh = Keire::ShaderGraphPreviewMesh::Plane,
        .Definition = &graph,
        .Properties = compilation.Properties,
        .Textures = textures,
        .Width = 96,
        .Height = 96,
        .RotationDegrees = 0.0F,
    };
    const auto textured = KeireEditor::RenderShaderGraphPreview(request);
    request.Textures = {};
    const auto fallback = KeireEditor::RenderShaderGraphPreview(request);
    CHECK(textured != fallback);
    const auto center = (48U * request.Width + 48U) * 4U;
    CHECK(std::to_integer<std::uint8_t>(textured[center]) > std::to_integer<std::uint8_t>(textured[center + 1U]));
}

TEST_CASE("Shader Graph live preview evaluates deep expression chains without recursive dispatcher frames")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    REQUIRE_FALSE(graph.Nodes.empty());
    const auto masterInput =
        std::ranges::find(graph.Nodes.front().Pins, std::string("BaseColor"), &Keire::ShaderGraphPin::Name);
    REQUIRE(masterInput != graph.Nodes.front().Pins.end());

    auto constant =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Color);
    constant.Value = Keire::Color{0.1F, 0.2F, 0.3F, 1.0F};
    const auto constantOutput =
        std::ranges::find(constant.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction);
    REQUIRE(constantOutput != constant.Pins.end());
    Keire::ShaderGraphEndpoint previous{constant.Id, constantOutput->Id};
    graph.Nodes.push_back(std::move(constant));

    constexpr std::size_t expressionDepth = 128;
    for (std::size_t index = 0; index < expressionDepth; ++index)
    {
        auto add = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add, Keire::ShaderGraphValueType::Color);
        const auto input = std::ranges::find(add.Pins, std::string("A"), &Keire::ShaderGraphPin::Name);
        const auto output =
            std::ranges::find(add.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction);
        REQUIRE(input != add.Pins.end());
        REQUIRE(output != add.Pins.end());
        graph.Connections.push_back({Keire::AssetId::Generate(), previous, {add.Id, input->Id}});
        previous = {add.Id, output->Id};
        graph.Nodes.push_back(std::move(add));
    }
    graph.Connections.push_back({Keire::AssetId::Generate(), previous, {graph.Nodes.front().Id, masterInput->Id}});

    const KeireEditor::ShaderGraphPreviewRequest request{
        .Output = graph.Output,
        .Mesh = Keire::ShaderGraphPreviewMesh::Plane,
        .Definition = &graph,
        .Width = 32,
        .Height = 32,
        .Exposure = 1.0F,
        .EnvironmentIntensity = 1.0F,
        .RotationDegrees = 0.0F,
    };
    const auto pixels = KeireEditor::RenderShaderGraphPreview(request);
    CHECK(pixels.size() == static_cast<std::size_t>(request.Width) * request.Height * 4U);
}

TEST_CASE("Shader Graph live preview resolves only the selected static-switch branch")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto custom = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Custom, Keire::ShaderGraphValueType::Color);
    auto switchNode =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::StaticSwitch, Keire::ShaderGraphValueType::Color);
    const auto pin = [](Keire::ShaderGraphNode& node, const std::string_view name) -> Keire::ShaderGraphPin&
    {
        const auto located = std::ranges::find(node.Pins, name, &Keire::ShaderGraphPin::Name);
        REQUIRE(located != node.Pins.end());
        return *located;
    };
    const auto endpoint = [&pin](Keire::ShaderGraphNode& node, const std::string_view name)
    { return Keire::ShaderGraphEndpoint{node.Id, pin(node, name).Id}; };

    pin(switchNode, "False").DefaultValue = Keire::Color{0.8F, 0.1F, 0.6F, 1.0F};
    graph.Connections.push_back({Keire::AssetId::Generate(), endpoint(custom, "Result"), endpoint(custom, "Input")});
    graph.Connections.push_back({Keire::AssetId::Generate(), endpoint(custom, "Result"), endpoint(switchNode, "True")});
    graph.Connections.push_back(
        {Keire::AssetId::Generate(), endpoint(switchNode, "Result"), endpoint(graph.Nodes.front(), "BaseColor")});
    graph.Nodes.push_back(std::move(custom));
    graph.Nodes.push_back(std::move(switchNode));

    auto reference = Keire::CreateDefaultShaderGraph();
    auto constant =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Color);
    constant.Value = Keire::Color{0.8F, 0.1F, 0.6F, 1.0F};
    reference.Connections.push_back(
        {Keire::AssetId::Generate(), endpoint(constant, "Value"), endpoint(reference.Nodes.front(), "BaseColor")});
    reference.Nodes.push_back(std::move(constant));

    KeireEditor::ShaderGraphPreviewRequest request{.Output = graph.Output,
                                                   .Mesh = Keire::ShaderGraphPreviewMesh::Plane,
                                                   .Definition = &graph,
                                                   .Width = 32,
                                                   .Height = 32,
                                                   .Exposure = 1.0F,
                                                   .EnvironmentIntensity = 1.0F,
                                                   .RotationDegrees = 0.0F};
    const auto evaluated = KeireEditor::RenderShaderGraphPreview(request);
    request.Definition = &reference;
    CHECK(KeireEditor::RenderShaderGraphPreview(request) == evaluated);
}

TEST_CASE("Shader Graph Sandbox progression compiles without dead authored work")
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
    KeireEditor::ShaderGraphDocument document({.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        const auto bytes = ReadBytes(root / names[index]);
        const auto graph = Keire::ShaderGraphAsset::DecodeSource(bytes);
        INFO(names[index]);
        CHECK(graph.Nodes.size() == expectedNodes[index]);
        const auto compilation = Keire::CompileShaderGraph(graph);
        const auto diagnostic =
            compilation.Diagnostics.empty() ? std::string{} : compilation.Diagnostics.front().Message;
        INFO(diagnostic);
        REQUIRE(compilation.Succeeded());
        CHECK(compilation.Statistics.UnusedNodeCount == 0);
        CHECK(compilation.Statistics.VariantCount == 1);
        document.Open(Keire::AssetId::Generate(), bytes, index + 1U, {});
        const auto canvas = document.BuildCanvasModel();
        CHECK(canvas.Nodes.size() == graph.Nodes.size());
        CHECK(canvas.Connections.size() == graph.Connections.size());
        document.Close();
    }
}

TEST_CASE("Shader Graph hero example imports compiled shader and runtime material subassets")
{
    const auto path = std::filesystem::current_path() /
                      "Samples/KeireSandbox/Assets/Examples/MaterialLab/ShaderGraphs/03_Advanced/"
                      "SG_12_IridescentShield.keireshadergraph";
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
    context.ResolveSubAssetIdFor = [&generatedIds](const Keire::AssetId owner, const std::string_view key)
    {
        const auto qualifiedKey = owner.ToString() + "/" + std::string(key);
        return generatedIds.try_emplace(qualifiedKey, Keire::AssetId::Generate()).first->second;
    };
    const auto importer = Keire::CreateShaderGraphAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport(context, bytes);
    REQUIRE(imported.SubAssets.size() == 2);

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
    CHECK(shaders == 1);
    REQUIRE(runtimeMaterial != nullptr);
    const auto material = Keire::MaterialAsset::Decode(runtimeMaterial->Bytes);
    REQUIRE(material);
    CHECK(material->Definition().Shader);
    CHECK(std::ranges::find(runtimeMaterial->AssetDependencies, material->Definition().Shader) !=
          runtimeMaterial->AssetDependencies.end());
}
