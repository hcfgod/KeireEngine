#include "KeireClient/Editor/ShaderGraphDocument.h"
#include "KeireClient/Editor/ShaderGraphPreview.h"
#include "KeireClient/Editor/ShaderGraphPreviewTextureSampling.h"
#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorInternal.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
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

TEST_CASE("Shader Graph first save compiles executable variants before reusing unchanged source")
{
    std::vector<std::size_t> publications;
    KeireEditor::ShaderGraphDocument document(
        {.LiveApply = [&publications](Keire::AssetId, const Keire::ShaderGraphDefinition&,
                                      const Keire::ShaderGraphCompilation&,
                                      const std::span<const Keire::Ref<Keire::ShaderAsset>> shaders)
         { publications.push_back(shaders.size()); },
         .Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    document.Create(Keire::AssetId::Generate());
    REQUIRE(publications == std::vector<std::size_t>{0});
    document.Save();
    REQUIRE(publications == std::vector<std::size_t>{0, 1});
    document.Save();
    CHECK(publications == std::vector<std::size_t>{0, 1, 0});
    document.ApplyLiveRevision();
    CHECK(publications.back() == 1);
}

TEST_CASE("Shader Graph CPU regeneration invalidates executable variants when the manifest changes")
{
    std::vector<std::size_t> publications;
    KeireEditor::ShaderGraphDocument document(
        {.LiveApply = [&](Keire::AssetId, const Keire::ShaderGraphDefinition&, const Keire::ShaderGraphCompilation&,
                          const std::span<const Keire::Ref<Keire::ShaderAsset>> shaders)
         { publications.push_back(shaders.size()); },
         .Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    document.Create(Keire::AssetId::Generate());
    document.Save();
    REQUIRE(publications.back() == 1);
    const auto hlsl = document.LastGoodCompilation()->Variants.front().Hlsl;
    const auto manifest = document.LastGoodCompilation()->Variants.front().Manifest;
    document.SetCompileOptions({.GeneratedSource = "Assets/Generated/Relocated.hlsl"});
    CHECK(document.LastGoodCompilation()->Variants.front().Hlsl == hlsl);
    CHECK(document.LastGoodCompilation()->Variants.front().Manifest != manifest);
    document.ApplyLiveRevision();
    CHECK(publications.back() == 0);
    document.Save();
    CHECK(publications.back() == 1);
    document.Save();
    CHECK(publications.back() == 0);
}

TEST_CASE("Shader Graph superseding edits cancel backend work after graph generation")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto custom = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Custom, Keire::ShaderGraphValueType::Color);
    custom.Include = "Assets/Shaders/Nodes/Cancelled.hlsli";
    graph.Nodes.push_back(custom);
    const std::string include = "float4 EvaluateCustomMaterialNode(float4 value) { return value; }\n";
    std::size_t generationReads = 0;
    REQUIRE(Keire::CompileShaderGraph(graph, {.ReadInclude =
                                                  [&](const std::filesystem::path&)
                                              {
                                                  ++generationReads;
                                                  return std::optional(include);
                                              }})
                .Succeeded());
    REQUIRE(generationReads > 0);

    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    std::size_t reads = 0;
    std::size_t publications = 0;
    KeireEditor::ShaderGraphDocument document(
        {.CompileOptions = {.ReadInclude =
                                [&](const std::filesystem::path&)
                            {
                                std::unique_lock lock(mutex);
                                ++reads;
                                entered = true;
                                changed.notify_all();
                                changed.wait_for(lock, std::chrono::seconds(5), [&] { return released; });
                                return std::optional(include);
                            }},
         .LiveApply = [&](Keire::AssetId, const Keire::ShaderGraphDefinition&, const Keire::ShaderGraphCompilation&,
                          std::span<const Keire::Ref<Keire::ShaderAsset>>) { ++publications; },
         .Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    document.Create(Keire::AssetId::Generate());
    REQUIRE(document.AddNode(custom));
    document.AdvanceCompilation(0.075);
    bool workerEntered = false;
    {
        std::unique_lock lock(mutex);
        workerEntered = changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
    }
    const bool removed = document.RemoveNode(custom.Id);
    {
        std::scoped_lock lock(mutex);
        released = true;
    }
    changed.notify_all();
    REQUIRE(workerEntered);
    REQUIRE(removed);
    DrainCompilation(document);
    CHECK(document.Publishable());
    CHECK(publications == 2);
    CHECK(reads == generationReads);
    CHECK(document.LastGoodDefinition()->Nodes.size() == 1);
}

TEST_CASE("Shader Graph save validates changed include bytes while preserving last-good executable variants")
{
    std::string include = "float4 EvaluateCustomMaterialNode(float4 value) { return value; }\n";
    std::size_t persisted = 0;
    std::vector<std::size_t> publications;
    KeireEditor::ShaderGraphDocument document(
        {.CompileOptions = {.ReadInclude = [&](const std::filesystem::path& path) -> std::optional<std::string>
                            {
                                if (path.lexically_normal() == "Assets/Shaders/Nodes/Mutable.hlsli")
                                    return include;
                                return std::nullopt;
                            }},
         .LiveApply = [&](Keire::AssetId, const Keire::ShaderGraphDefinition&, const Keire::ShaderGraphCompilation&,
                          const std::span<const Keire::Ref<Keire::ShaderAsset>> shaders)
         { publications.push_back(shaders.size()); },
         .Persist = [&](Keire::AssetId, std::span<const std::byte>) { ++persisted; }});
    auto graph = Keire::CreateDefaultShaderGraph();
    auto custom = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Custom, Keire::ShaderGraphValueType::Color);
    custom.Include = "Assets/Shaders/Nodes/Mutable.hlsli";
    graph.Nodes.push_back(custom);
    document.Create(Keire::AssetId::Generate(), graph);
    INFO(document.Diagnostic());
    REQUIRE_NOTHROW(document.Save());
    REQUIRE(document.Publishable());
    REQUIRE(publications.back() == 1);
    const auto successfulPublications = publications.size();
    const auto lastGoodHlsl = document.LastGoodCompilation()->Variants.front().Hlsl;

    include = "#error Stale_dependency_content\n";
    CHECK_THROWS_AS(document.Save(), std::logic_error);
    CHECK_FALSE(document.Publishable());
    CHECK(persisted == 1);
    CHECK(publications.size() == successfulPublications);
    CHECK(document.LastGoodCompilation()->Variants.front().Hlsl == lastGoodHlsl);
    document.ApplyLiveRevision();
    CHECK(publications.back() == 1);

    include = "float4 EvaluateCustomMaterialNode(float4 value) { return value * 0.5; }\n";
    document.Save();
    CHECK(document.Publishable());
    CHECK(persisted == 2);
    CHECK(publications.back() == 1);
}

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

TEST_CASE("Shader Graph document batches multi-node movement and deletion into atomic undo commands")
{
    KeireEditor::ShaderGraphDocument document(
        {.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Shader Graph batch edits"});
    document.Create(Keire::AssetId::Generate(), Keire::CreateDefaultShaderGraph(), undo);
    auto first =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    auto second =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    first.EditorPosition = {40.0F, 80.0F};
    second.EditorPosition = {180.0F, 120.0F};
    REQUIRE(document.AddNode(first));
    REQUIRE(document.AddNode(second));

    const std::array moves{std::pair{first.Id, Keire::Vector2{140.0F, 180.0F}},
                           std::pair{second.Id, Keire::Vector2{280.0F, 220.0F}}};
    REQUIRE(document.MoveNodes(moves));
    CHECK(std::ranges::find(document.Definition().Nodes, first.Id, &Keire::ShaderGraphNode::Id)->EditorPosition ==
          moves[0].second);
    CHECK(std::ranges::find(document.Definition().Nodes, second.Id, &Keire::ShaderGraphNode::Id)->EditorPosition ==
          moves[1].second);
    REQUIRE(document.Undo());
    CHECK(std::ranges::find(document.Definition().Nodes, first.Id, &Keire::ShaderGraphNode::Id)->EditorPosition ==
          first.EditorPosition);
    CHECK(std::ranges::find(document.Definition().Nodes, second.Id, &Keire::ShaderGraphNode::Id)->EditorPosition ==
          second.EditorPosition);
    REQUIRE(document.Redo());

    REQUIRE(document.Edit("Create selection comment",
                          [&](auto& definition)
                          {
                              definition.Authoring.Comments.push_back({Keire::AssetId::Generate(),
                                                                       "Values",
                                                                       {},
                                                                       {},
                                                                       {360.0F, 240.0F},
                                                                       {},
                                                                       18.0F,
                                                                       Keire::GraphCommentMoveMode::Group,
                                                                       {},
                                                                       {first.Id, second.Id},
                                                                       false});
                          }));
    const std::array selected{first.Id, second.Id};
    REQUIRE(document.RemoveNodes(selected));
    CHECK(std::ranges::find(document.Definition().Nodes, first.Id, &Keire::ShaderGraphNode::Id) ==
          document.Definition().Nodes.end());
    CHECK(document.Definition().Authoring.Comments.front().Members.empty());
    REQUIRE(document.Undo());
    CHECK(std::ranges::find(document.Definition().Nodes, first.Id, &Keire::ShaderGraphNode::Id) !=
          document.Definition().Nodes.end());
    CHECK(document.Definition().Authoring.Comments.front().Members == std::vector{first.Id, second.Id});

    const std::array includesOutput{first.Id, document.Definition().Nodes.front().Id};
    const auto before = document.Definition();
    CHECK_THROWS_AS((void)document.RemoveNodes(includesOutput), std::invalid_argument);
    CHECK(document.Definition() == before);
    undoService->Close();
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

TEST_CASE("Shader Graph live apply republishes culling bounds and stable property identities without HLSL changes")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "Shader metadata edits"});
    std::vector<Keire::Ref<Keire::ShaderAsset>> published;
    KeireEditor::ShaderGraphDocument document(
        {.LiveApply = [&published](Keire::AssetId, const Keire::ShaderGraphDefinition&,
                                   const Keire::ShaderGraphCompilation&,
                                   const std::span<const Keire::Ref<Keire::ShaderAsset>> shaders)
         { published.insert(published.end(), shaders.begin(), shaders.end()); },
         .Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    document.Create(Keire::AssetId::Generate(), Keire::CreateDefaultShaderGraph(), undo);
    auto offset =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Vector3);
    offset.Symbol = "Offset";
    offset.Value = Keire::Vector3{0.1F, 0.0F, 0.0F};
    REQUIRE(document.AddNode(offset));
    const auto& master = document.Definition().Nodes.front();
    const auto input = std::ranges::find(master.Pins, "WorldPositionOffset", &Keire::ShaderGraphPin::Name);
    REQUIRE(input != master.Pins.end());
    REQUIRE(document.AddConnection({{}, {offset.Id, offset.Pins.front().Id}, {master.Id, input->Id}}));
    DrainCompilation(document);
    REQUIRE(published.size() == 1);
    CHECK_FALSE(published.back()->Definition().MaximumWorldPositionDisplacementRadius);
    REQUIRE(published.back()->Definition().Properties.size() == 1);
    CHECK(published.back()->Definition().Properties.front().Id == offset.Id);
    const auto originalHlsl = document.Compilation().Variants.front().Hlsl;

    REQUIRE(document.Edit("Set displacement bound", [](Keire::ShaderGraphDefinition& graph)
                          { graph.MaximumWorldPositionDisplacementRadius = 2.0F; }));
    DrainCompilation(document);
    REQUIRE(published.size() == 2);
    REQUIRE(published.back()->Definition().MaximumWorldPositionDisplacementRadius);
    CHECK(*published.back()->Definition().MaximumWorldPositionDisplacementRadius == doctest::Approx(2.0F));
    CHECK(document.Compilation().Variants.front().Hlsl == originalHlsl);

    const auto replacementId = Keire::AssetId::Generate();
    REQUIRE(document.Edit("Replace parameter identity",
                          [&](Keire::ShaderGraphDefinition& graph)
                          {
                              auto parameter = std::ranges::find(graph.Nodes, offset.Id, &Keire::ShaderGraphNode::Id);
                              REQUIRE(parameter != graph.Nodes.end());
                              parameter->Id = replacementId;
                              for (auto& connection : graph.Connections)
                                  if (connection.Output.Node == offset.Id)
                                      connection.Output.Node = replacementId;
                          }));
    DrainCompilation(document);
    REQUIRE(published.size() == 3);
    REQUIRE(published.back()->Definition().Properties.size() == 1);
    CHECK(published.back()->Definition().Properties.front().Id == replacementId);
    CHECK(document.Compilation().Variants.front().Hlsl == originalHlsl);
    REQUIRE(document.Undo());
    DrainCompilation(document);
    REQUIRE(published.size() == 4);
    CHECK(published.back()->Definition().Properties.front().Id == offset.Id);
    document.Close();
}

TEST_CASE("Shader Graph output inputs drive compiled values and context undo stays in the edited document")
{
    auto graph = Keire::CreateShaderGraphTemplate(Keire::ShaderGraphTemplate::Fullscreen);
    const auto baseline = Keire::CompileShaderGraph(graph);
    REQUIRE(baseline.Succeeded());
    REQUIRE_FALSE(baseline.Variants.empty());
    graph.Nodes.front().Value = Keire::Color{1.0F, 0.0F, 0.0F, 1.0F};
    const auto unusedValue = Keire::CompileShaderGraph(graph);
    REQUIRE(unusedValue.Succeeded());
    CHECK(unusedValue.Variants.front().Hlsl == baseline.Variants.front().Hlsl);

    auto service = Keire::CreateRef<Keire::UndoService>();
    KeireEditor::ShaderGraphDocument other({.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    other.Create(Keire::AssetId::Generate(), graph, service->CreateContext({.Name = "Other graph"}));
    REQUIRE(other.Edit("Rename other output", [](auto& definition) { definition.Nodes.front().Name = "Other"; }));
    const auto otherRevision = other.Definition();
    KeireEditor::ShaderGraphDocument edited({.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    edited.Create(Keire::AssetId::Generate(), graph, service->CreateContext({.Name = "Focused graph"}));
    const auto original = edited.Definition();
    REQUIRE(edited.Edit("Edit output opacity",
                        [](auto& definition)
                        {
                            for (auto& pin : definition.Nodes.front().Pins)
                                if (pin.Name == "Opacity")
                                    pin.DefaultValue = 0.5F;
                        }));
    const auto changed = edited.Definition();
    DrainCompilation(edited);
    REQUIRE(edited.Compilation().Succeeded());
    CHECK(edited.Compilation().Variants.front().Hlsl != baseline.Variants.front().Hlsl);
    REQUIRE(edited.UndoContext()->Undo());
    CHECK(edited.Definition() == original);
    CHECK(other.Definition() == otherRevision);
    CHECK(other.UndoContext()->CanUndo());
    REQUIRE(edited.UndoContext()->Redo());
    CHECK(edited.Definition() == changed);
    CHECK(other.Definition() == otherRevision);
    edited.Close();
    other.Close();
    service->Close();
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

TEST_CASE("Shader Graph UI and fullscreen previews cover the image without mesh lighting")
{
    for (const auto graphTemplate : {Keire::ShaderGraphTemplate::Ui, Keire::ShaderGraphTemplate::Fullscreen})
    {
        auto graph = Keire::CreateShaderGraphTemplate(graphTemplate);
        for (auto& pin : graph.Nodes.front().Pins)
            if (pin.Name == "Color")
                pin.DefaultValue = Keire::Color{1.0F, 0.0F, 0.0F, 1.0F};
        KeireEditor::ShaderGraphPreviewRequest request{
            .Output = graph.Output, .Definition = &graph, .Width = 48, .Height = 32};
        const auto pixels = KeireEditor::RenderShaderGraphPreview(request);
        REQUIRE(pixels.size() == 48U * 32U * 4U);
        for (std::size_t offset = 0; offset < pixels.size(); offset += 4)
        {
            REQUIRE(pixels[offset] == std::byte{255});
            REQUIRE(pixels[offset + 1] == std::byte{0});
            REQUIRE(pixels[offset + 2] == std::byte{0});
            REQUIRE(pixels[offset + 3] == std::byte{255});
        }
        request.Mesh = Keire::ShaderGraphPreviewMesh::Custom;
        request.RotationDegrees = 170.0F;
        request.EnvironmentIntensity = 0.0F;
        CHECK(KeireEditor::RenderShaderGraphPreview(request) == pixels);
        for (auto& pin : graph.Nodes.front().Pins)
            if (pin.Name == "Opacity")
                pin.DefaultValue = 0.0F;
        CHECK(KeireEditor::RenderShaderGraphPreview(request) != pixels);
        std::size_t cancellationChecks = 0;
        request.CancellationRequested = [&] { return ++cancellationChecks > request.Height; };
        CHECK_THROWS_AS((void)KeireEditor::RenderShaderGraphPreview(request), std::runtime_error);
    }
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

TEST_CASE("Shader Graph texture preview decodes sRGB color without changing linear alpha")
{
    Keire::TextureImportSettings settings;
    settings.Mips = Keire::TextureMipPolicy::None;
    Keire::TextureMipLevel mip{1, 1, {std::byte{128}, std::byte{10}, std::byte{255}, std::byte{128}}};
    float expectedRed = 0.2158605F;
    float expectedGreen = 0.00303527F;
    SUBCASE("sRGB") {}
    SUBCASE("Linear")
    {
        settings.ColorSpace = Keire::TextureColorSpace::Linear;
        expectedRed = 128.0F / 255.0F;
        expectedGreen = 10.0F / 255.0F;
    }
    const auto asset = Keire::AssetId::Generate();
    const std::array textures{KeireEditor::ShaderGraphPreviewTexture{
        asset, Keire::CreateRef<Keire::Texture2DAsset>(settings, std::vector{mip})}};
    const auto sample = KeireEditor::Detail::SampleShaderGraphPreviewTexture(textures, asset, {0.5F, 0.5F});
    REQUIRE(sample);
    CHECK(sample->X == doctest::Approx(expectedRed));
    CHECK(sample->Y == doctest::Approx(expectedGreen));
    CHECK(sample->Z == doctest::Approx(1.0F));
    CHECK(sample->W == doctest::Approx(128.0F / 255.0F));
}

TEST_CASE("Shader Graph texture preview honors each axis address mode")
{
    Keire::TextureImportSettings settings;
    settings.Mips = Keire::TextureMipPolicy::None;
    settings.ColorSpace = Keire::TextureColorSpace::Linear;
    settings.Sampler.Magnification = Keire::TextureFilter::Nearest;
    const Keire::TextureMipLevel mip{2,
                                     2,
                                     {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255},
                                      std::byte{0}, std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255},
                                      std::byte{0}, std::byte{255}, std::byte{255}, std::byte{255}, std::byte{0},
                                      std::byte{255}}};
    Keire::Vector2 uv{1.25F, -0.25F};
    Keire::Vector2 expected{0.0F, 1.0F};
    SUBCASE("Repeat") {}
    SUBCASE("Clamp U while V repeats")
    {
        settings.Sampler.AddressU = Keire::TextureAddressMode::Clamp;
        expected = {1.0F, 1.0F};
    }
    SUBCASE("Mirror positive and negative coordinates")
    {
        settings.Sampler.AddressU = Keire::TextureAddressMode::Mirror;
        settings.Sampler.AddressV = Keire::TextureAddressMode::Mirror;
        expected = {1.0F, 0.0F};
    }
    SUBCASE("Mirror across multiple periods")
    {
        settings.Sampler.AddressU = Keire::TextureAddressMode::Mirror;
        settings.Sampler.AddressV = Keire::TextureAddressMode::Mirror;
        uv = {-1.25F, 2.25F};
        expected = {1.0F, 0.0F};
    }
    const auto asset = Keire::AssetId::Generate();
    const std::array textures{KeireEditor::ShaderGraphPreviewTexture{
        asset, Keire::CreateRef<Keire::Texture2DAsset>(settings, std::vector{mip})}};
    const auto sample = KeireEditor::Detail::SampleShaderGraphPreviewTexture(textures, asset, uv);
    REQUIRE(sample);
    CHECK(sample->X == expected.X);
    CHECK(sample->Y == expected.Y);
}

TEST_CASE("Shader Graph texture preview filters decoded texels and applies address modes at seams")
{
    Keire::TextureImportSettings settings;
    settings.Mips = Keire::TextureMipPolicy::None;
    const Keire::TextureMipLevel mip{2,
                                     1,
                                     {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{128},
                                      std::byte{128}, std::byte{128}, std::byte{255}}};
    Keire::Vector2 uv{0.5F, 0.5F};
    float expectedRed = 0.2158605F * 0.5F;
    float expectedAlpha = 0.5F;
    SUBCASE("Bilinear midpoint") {}
    SUBCASE("Repeat seam") { uv.X = 0.0F; }
    SUBCASE("Clamp seam")
    {
        uv.X = 0.0F;
        settings.Sampler.AddressU = Keire::TextureAddressMode::Clamp;
        expectedRed = expectedAlpha = 0.0F;
    }
    SUBCASE("Mirror seam")
    {
        uv.X = 1.0F;
        settings.Sampler.AddressU = Keire::TextureAddressMode::Mirror;
        expectedRed = 0.2158605F;
        expectedAlpha = 1.0F;
    }
    const auto asset = Keire::AssetId::Generate();
    const std::array textures{KeireEditor::ShaderGraphPreviewTexture{
        asset, Keire::CreateRef<Keire::Texture2DAsset>(settings, std::vector{mip})}};
    const auto sample = KeireEditor::Detail::SampleShaderGraphPreviewTexture(textures, asset, uv);
    REQUIRE(sample);
    CHECK(sample->X == doctest::Approx(expectedRed));
    CHECK(sample->W == doctest::Approx(expectedAlpha));
}

TEST_CASE("Shader Graph texture preview handles HDR and non-finite coordinates deterministically")
{
    Keire::TextureImportSettings settings;
    settings.Mips = Keire::TextureMipPolicy::None;
    settings.Semantic = Keire::TextureSemantic::Environment;
    settings.HighDynamicRange = true;
    const auto asset = Keire::AssetId::Generate();
    Keire::TextureMipLevel mip{1, 1, {std::byte{128}, std::byte{64}, std::byte{32}, std::byte{130}}};
    float expected = 2.0F;
    SUBCASE("RGBE radiance") {}
    SUBCASE("Zero exponent encodes black")
    {
        mip.Pixels[3] = std::byte{0};
        expected = 0.0F;
    }
    const std::array textures{KeireEditor::ShaderGraphPreviewTexture{
        asset, Keire::CreateRef<Keire::Texture2DAsset>(settings, std::vector{mip})}};
    const auto sample = KeireEditor::Detail::SampleShaderGraphPreviewTexture(
        textures, asset, {std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()});
    REQUIRE(sample);
    CHECK(sample->X == expected);
    CHECK(sample->Y == expected * 0.5F);
    CHECK(sample->Z == expected * 0.25F);
    CHECK(sample->W == 1.0F);
    for (const float invalid : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::quiet_NaN()})
    {
        CHECK_FALSE(KeireEditor::Detail::SampleShaderGraphPreviewTexture(textures, asset, {invalid, 0.0F}));
        CHECK_FALSE(KeireEditor::Detail::SampleShaderGraphPreviewTexture(textures, asset, {0.0F, invalid}));
    }
}

TEST_CASE("Shader Graph missing-texture preview tolerates overflowing UV expressions")
{
    auto graph = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    auto texture = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::TextureSample);
    const auto color = std::ranges::find(graph.Nodes.front().Pins, "Color", &Keire::ShaderGraphPin::Name);
    const auto output = std::ranges::find(texture.Pins, "RGBA", &Keire::ShaderGraphPin::Name);
    REQUIRE(color != graph.Nodes.front().Pins.end());
    REQUIRE(output != texture.Pins.end());
    graph.Connections.push_back(
        {Keire::AssetId::Generate(), {texture.Id, output->Id}, {graph.Nodes.front().Id, color->Id}});
    const auto uv = std::ranges::find(texture.Pins, "UV", &Keire::ShaderGraphPin::Name);
    REQUIRE(uv != texture.Pins.end());
    uv->DefaultValue = Keire::Vector2{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    graph.Nodes.push_back(texture);
    KeireEditor::ShaderGraphPreviewRequest request{.Output = graph.Output, .Definition = &graph};
    KeireEditor::ShaderGraphPreviewInternal::ShaderGraphPreviewEvaluator evaluator(request);
    const auto result = evaluator.Resolve({}, {0.0F, 0.0F, 1.0F}, {});
    CHECK(result.BaseColor.X == doctest::Approx(0.88F));
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

TEST_CASE("Shader Graph live preview evaluates OpenPBR slab composition")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto first = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::OpenPbrSurfaceBsdf);
    auto second = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::OpenPbrSurfaceBsdf);
    auto mix = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::MixSlabs);
    auto attributes = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::BsdfToMaterialAttributes);
    const auto pin = [](Keire::ShaderGraphNode& node, const std::string_view name) -> Keire::ShaderGraphPin&
    {
        const auto located = std::ranges::find(node.Pins, name, &Keire::ShaderGraphPin::Name);
        REQUIRE(located != node.Pins.end());
        return *located;
    };
    const auto endpoint = [&pin](Keire::ShaderGraphNode& node, const std::string_view name)
    { return Keire::ShaderGraphEndpoint{node.Id, pin(node, name).Id}; };
    pin(first, "BaseColor").DefaultValue = Keire::Color{0.85F, 0.12F, 0.04F, 1.0F};
    pin(second, "BaseColor").DefaultValue = Keire::Color{0.05F, 0.25F, 0.9F, 1.0F};
    pin(second, "Metallic").DefaultValue = 1.0F;
    pin(mix, "Factor").DefaultValue = 0.35F;
    graph.Connections.push_back({Keire::AssetId::Generate(), endpoint(first, "Slab"), endpoint(mix, "A")});
    graph.Connections.push_back({Keire::AssetId::Generate(), endpoint(second, "Slab"), endpoint(mix, "B")});
    graph.Connections.push_back({Keire::AssetId::Generate(), endpoint(mix, "Slab"), endpoint(attributes, "BSDF")});
    graph.Connections.push_back({Keire::AssetId::Generate(), endpoint(attributes, "Attributes"),
                                 endpoint(graph.Nodes.front(), "MaterialAttributes")});
    graph.Nodes.insert(graph.Nodes.end(), {std::move(first), std::move(second), std::move(mix), std::move(attributes)});

    const KeireEditor::ShaderGraphPreviewRequest request{.Output = graph.Output,
                                                         .Mesh = Keire::ShaderGraphPreviewMesh::Sphere,
                                                         .Definition = &graph,
                                                         .Width = 32,
                                                         .Height = 32,
                                                         .Exposure = 1.0F,
                                                         .EnvironmentIntensity = 1.0F,
                                                         .RotationDegrees = 0.0F};
    const auto pixels = KeireEditor::RenderShaderGraphPreview(request);
    CHECK(pixels.size() == static_cast<std::size_t>(request.Width) * request.Height * 4U);
    CHECK(std::ranges::any_of(pixels, [](const std::byte channel) { return channel != std::byte{0}; }));
    CHECK(KeireEditor::RenderShaderGraphPreview(request) == pixels);
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
            for (const auto format :
                 {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV, Keire::ShaderBinaryFormat::Msl})
                CHECK(shader->Variant(format, "forwardTransparent") != nullptr);
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

TEST_CASE("Material sphere previews preserve geometric lighting with neutral tangent normals")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    KeireEditor::ShaderGraphPreviewRequest request{.Output = graph.Output,
                                                   .Mesh = Keire::ShaderGraphPreviewMesh::Sphere,
                                                   .Definition = &graph,
                                                   .Width = 96,
                                                   .Height = 96};
    const auto neutral = KeireEditor::RenderShaderGraphPreview(request);
    const auto red = [&](std::size_t x, std::size_t y) { return std::to_integer<int>(neutral[(y * 96 + x) * 4]); };
    CHECK(std::abs(red(33, 33) - red(63, 63)) > 10);
    auto normal =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Vector3);
    normal.Value = Keire::Vector3{0.0F, 0.0F, 1.0F};
    const auto input = std::ranges::find(graph.Nodes.front().Pins, "Normal", &Keire::ShaderGraphPin::Name);
    REQUIRE(input != graph.Nodes.front().Pins.end());
    graph.Connections.push_back(
        {Keire::AssetId::Generate(), {normal.Id, normal.Pins.front().Id}, {graph.Nodes.front().Id, input->Id}});
    graph.Nodes.push_back(std::move(normal));
    CHECK(KeireEditor::RenderShaderGraphPreview(request) == neutral);
    graph.Nodes.back().Value = Keire::Vector3{0.7F, 0.0F, 0.7F};
    CHECK(KeireEditor::RenderShaderGraphPreview(request) != neutral);
}
