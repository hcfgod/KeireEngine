#include "KeireClient/Editor/ShaderGraphDocument.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace
{
    Keire::ShaderGraphEndpoint Pin(const Keire::ShaderGraphNode& node, const std::string_view name)
    {
        const auto pin = std::ranges::find(node.Pins, name, &Keire::ShaderGraphPin::Name);
        if (pin == node.Pins.end())
            throw std::logic_error("Test fixture pin is unavailable.");
        return {node.Id, pin->Id};
    }

    struct AuthoringFixture
    {
        Keire::Ref<Keire::UndoService> UndoService = Keire::CreateRef<Keire::UndoService>();
        Keire::Ref<Keire::UndoContext> Undo = UndoService->CreateContext({.Name = "Connected graph creation"});
        KeireEditor::ShaderGraphDocument Document{{.Persist = [](Keire::AssetId, std::span<const std::byte>) {}}};
        Keire::ShaderGraphDefinition Graph = Keire::CreateDefaultShaderGraph();
        Keire::ShaderGraphNode Source =
            Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);

        AuthoringFixture()
        {
            Graph.Nodes.push_back(Source);
            Graph.Connections.push_back({Keire::AssetId::Generate(),
                                         {Source.Id, Source.Pins.front().Id},
                                         Pin(Graph.Nodes.front(), "Roughness"),
                                         {{123.0F, 45.0F}}});
            Document.Open(Keire::AssetId::Generate(), Graph, 1, Undo);
        }
    };
} // namespace

TEST_CASE("Shader Graph connected creation replaces an input atomically and restores its cable with undo")
{
    AuthoringFixture fixture;
    auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    node.Value = 0.73F;
    REQUIRE(fixture.Document.AddConnectedNode(node, fixture.Graph.Connections.front().Input));
    const auto connected = fixture.Document.Definition();
    REQUIRE(connected.Connections.size() == 1);
    CHECK(connected.Connections.front().Output.Node == node.Id);
    CHECK(connected.Connections.front().Input == fixture.Graph.Connections.front().Input);
    CHECK(connected.Nodes.size() == fixture.Graph.Nodes.size() + 1);
    CHECK(fixture.Document.Dirty());
    REQUIRE(fixture.Document.Undo());
    CHECK(fixture.Document.Definition() == fixture.Graph);
    CHECK_FALSE(fixture.Document.Dirty());
    REQUIRE(fixture.Document.Redo());
    CHECK(fixture.Document.Definition() == connected);
}

TEST_CASE("Shader Graph connected creation from an output preserves existing fanout")
{
    AuthoringFixture fixture;
    auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Multiply, Keire::ShaderGraphValueType::Scalar);
    REQUIRE(fixture.Document.AddConnectedNode(node, fixture.Graph.Connections.front().Output));
    const auto& graph = fixture.Document.Definition();
    REQUIRE(graph.Connections.size() == 2);
    CHECK(graph.Connections.front() == fixture.Graph.Connections.front());
    CHECK(graph.Connections.back().Output == fixture.Graph.Connections.front().Output);
    CHECK(graph.Connections.back().Input.Node == node.Id);
    REQUIRE(fixture.Document.Undo());
    CHECK(fixture.Document.Definition() == fixture.Graph);
}

TEST_CASE("Shader Graph cable insertion preserves downstream identity routing and exact undo redo")
{
    AuthoringFixture fixture;
    auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Multiply, Keire::ShaderGraphValueType::Scalar);
    REQUIRE(fixture.Document.InsertNode(node, fixture.Graph.Connections.front().Id));
    const auto inserted = fixture.Document.Definition();
    REQUIRE(inserted.Connections.size() == 2);
    const auto& original = fixture.Graph.Connections.front();
    CHECK(inserted.Connections.front().Id == original.Id);
    CHECK(inserted.Connections.front().RoutingPoints == original.RoutingPoints);
    CHECK(inserted.Connections.front().Input == original.Input);
    CHECK(inserted.Connections.front().Output.Node == node.Id);
    CHECK(inserted.Connections.back().Output == original.Output);
    CHECK(inserted.Connections.back().Input.Node == node.Id);
    CHECK(inserted.Connections.back().Id != original.Id);
    const auto canvas = fixture.Document.BuildCanvasModel();
    CHECK(canvas.Connections.size() == 2);
    REQUIRE(fixture.Document.Undo());
    CHECK(fixture.Document.Definition() == fixture.Graph);
    REQUIRE(fixture.Document.Redo());
    CHECK(fixture.Document.Definition() == inserted);
    CHECK(Keire::ShaderGraphAsset::DecodeSource(Keire::ShaderGraphAsset::EncodeSource(inserted)) == inserted);
}

TEST_CASE("Shader Graph connected creation rejects incompatible stale and duplicate identities without partial edits")
{
    AuthoringFixture fixture;
    auto texture =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Texture2D);
    CHECK_THROWS_AS(fixture.Document.AddConnectedNode(texture, fixture.Graph.Connections.front().Input),
                    std::invalid_argument);
    auto scalar =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    CHECK_THROWS_AS(fixture.Document.AddConnectedNode(scalar, {Keire::AssetId::Generate(), scalar.Pins.front().Id}),
                    std::invalid_argument);
    CHECK_THROWS_AS(fixture.Document.AddConnectedNode(scalar, {fixture.Source.Id, Keire::AssetId::Generate()}),
                    std::invalid_argument);
    CHECK_THROWS_AS(fixture.Document.AddConnectedNode(fixture.Source, fixture.Graph.Connections.front().Input),
                    std::invalid_argument);
    CHECK_THROWS_AS(fixture.Document.InsertNode(scalar, fixture.Graph.Connections.front().Id), std::invalid_argument);
    CHECK_THROWS_AS(fixture.Document.InsertNode(scalar, Keire::AssetId::Generate()), std::invalid_argument);
    CHECK(fixture.Document.Definition() == fixture.Graph);
    CHECK_FALSE(fixture.Document.Dirty());
    CHECK_FALSE(fixture.Document.Undo());
}

TEST_CASE("Shader Graph connected keyword creation includes its declaration in the same undo transaction")
{
    AuthoringFixture fixture;
    auto keyword =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Keyword, Keire::ShaderGraphValueType::Scalar);
    keyword.Symbol = "CONNECTED_FEATURE";
    REQUIRE(fixture.Document.AddConnectedNode(keyword, fixture.Graph.Connections.front().Input));
    REQUIRE(fixture.Document.Definition().Keywords.size() == 1);
    CHECK(fixture.Document.Definition().Keywords.front().Name == keyword.Symbol);
    CHECK(fixture.Document.Definition().Connections.front().Output.Node == keyword.Id);
    REQUIRE(fixture.Document.Undo());
    CHECK(fixture.Document.Definition() == fixture.Graph);
    REQUIRE(fixture.Document.Redo());
    CHECK(fixture.Document.Definition().Keywords.size() == 1);
}

TEST_CASE("Shader Graph cable insertion accepts typed reusable function pins")
{
    AuthoringFixture fixture;
    const auto function = Keire::CreateDefaultGraphFunction(Keire::ShaderGraphPurpose::ShaderFunction);
    auto call = Keire::CreateShaderGraphFunctionCallNode(Keire::AssetId::Generate(), function.Body);
    // Use a color cable so both sides match the function's declared color interface.
    auto graph = fixture.Graph;
    graph.Connections.front().Input = Pin(graph.Nodes.front(), "BaseColor");
    fixture.Document.Open(Keire::AssetId::Generate(), graph, 1, fixture.Undo);
    REQUIRE(fixture.Document.InsertNode(call, graph.Connections.front().Id));
    CHECK(fixture.Document.Definition().Connections.front().Output.Node == call.Id);
    CHECK(fixture.Document.Definition().Connections.back().Input.Node == call.Id);
    REQUIRE(fixture.Document.Undo());
    CHECK(fixture.Document.Definition() == graph);
}

TEST_CASE("Shader Graph connected named keyword reuses its declared option without creating a boolean")
{
    AuthoringFixture fixture;
    auto graph = fixture.Graph;
    graph.Keywords.push_back({.Name = "QUALITY", .Options = {"Low", "High"}, .DefaultOption = "High"});
    fixture.Document.Open(Keire::AssetId::Generate(), graph, 1, fixture.Undo);
    auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Keyword);
    node.Symbol = "QUALITY_High";
    REQUIRE(fixture.Document.AddConnectedNode(node, graph.Connections.front().Input));
    CHECK(fixture.Document.Definition().Keywords == graph.Keywords);
    CHECK(fixture.Document.Definition().Connections.front().Output.Node == node.Id);
    REQUIRE(fixture.Document.Undo());
    CHECK(fixture.Document.Definition() == graph);
}
