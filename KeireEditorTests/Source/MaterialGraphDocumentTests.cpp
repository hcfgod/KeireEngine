#include "KeireClient/Editor/MaterialGraphDocument.h"
#include "KeireClient/Editor/MaterialGraphPreview.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
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

    void WriteText(const std::filesystem::path& path, const std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output)
            throw std::runtime_error("Cannot write Material Graph editor test file.");
    }

    struct TemporaryDirectory final
    {
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("KeireMaterialGraphEditor-" + Keire::AssetId::Generate().ToString()))
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
} // namespace

TEST_CASE("Material Graph document reuses the stable canvas and preserves last-good preview")
{
    std::size_t previewCount = 0;
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
         .Persist =
             [&persistCount, &persisted](Keire::AssetId, const std::span<const std::byte> bytes)
         {
             ++persistCount;
             persisted.assign(bytes.begin(), bytes.end());
         }});

    document.Create(Keire::AssetId::Generate(), Keire::CreateDefaultMaterialGraph(), undo);
    REQUIRE(document.Publishable());
    REQUIRE(document.LastGoodCompilation());
    REQUIRE(document.LastGoodDefinition());
    CHECK(previewCount == 1);
    const auto initialPreviewDefinition = *document.LastGoodDefinition();
    const auto canvas = document.BuildCanvasModel();
    REQUIRE(canvas.Nodes.size() == 1);
    CHECK(canvas.Nodes.front().Label == "PBR Master");
    CHECK(canvas.Nodes.front().Pins.size() == document.Definition().Nodes.front().Pins.size());
    CHECK(canvas.Node(canvas.Nodes.front().Id) == document.Definition().Nodes.front().Id);

    auto custom =
        Keire::CreateMaterialGraphNode(Keire::MaterialGraphNodeKind::Custom, Keire::MaterialGraphValueType::Color);
    custom.Include = "Assets/Shaders/Nodes/Custom.hlsli";
    REQUIRE(document.AddNode(custom));
    CHECK_FALSE(document.Publishable());
    REQUIRE(document.LastGoodCompilation());
    REQUIRE(document.LastGoodDefinition());
    CHECK(*document.LastGoodDefinition() == initialPreviewDefinition);
    CHECK(previewCount == 1);
    CHECK_THROWS_AS(document.Save(), std::logic_error);

    includeAvailable = true;
    REQUIRE(document.EditNode(custom.Id, [](Keire::MaterialGraphNode& node) { node.Name = "Safe Custom Node"; }));
    REQUIRE(document.Publishable());
    CHECK(previewCount == 2);
    REQUIRE(document.Undo());
    CHECK(previewCount == 3);
    REQUIRE(document.Redo());
    CHECK(previewCount == 4);
    document.SetPreviewSettings({.Mesh = Keire::MaterialGraphPreviewMesh::Cube,
                                 .Exposure = 1.4F,
                                 .EnvironmentIntensity = 0.8F,
                                 .RotationDegrees = -25.0F});
    CHECK(previewCount == 5);
    CHECK_THROWS_AS(document.SetPreviewSettings({.Exposure = 0.0F}), std::invalid_argument);
    document.Save();
    CHECK(persistCount == 1);
    CHECK_FALSE(persisted.empty());
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
    }
}

TEST_CASE("Material Graph hero example compiles every variant through production shader backends")
{
    const auto path = std::filesystem::current_path() /
                      "Samples/KeireSandbox/Assets/Materials/MaterialGraphs/05_AdaptiveTechSurface.keirematerialgraph";
    const auto bytes = ReadBytes(path);
    const auto graph = Keire::MaterialGraphAsset::DecodeSource(bytes);
    Keire::MaterialGraphCompileOptions options;
    options.GeneratedSource = "Assets/Generated/HeroMaterial.hlsl";
    const auto compilation = Keire::CompileMaterialGraph(graph, options);
    REQUIRE(compilation.Succeeded());
    REQUIRE(compilation.Variants.size() == 2);

    TemporaryDirectory directory;
    for (const auto& variant : compilation.Variants)
    {
        const auto source = directory.Path / variant.GeneratedSource;
        auto manifest = source;
        manifest.replace_extension(".keireshader");
        WriteText(source, variant.Hlsl);
        WriteText(manifest, variant.Manifest);

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
    }
}
