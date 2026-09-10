#include "KeireClient/Editor/GraphDuplication.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>

TEST_CASE("Shader Graph selection duplication remaps topology and contained authoring metadata")
{
    Keire::ShaderGraphDefinition definition;
    Keire::ShaderGraphNode first;
    first.Id = Keire::AssetId::Generate();
    first.Name = "First";
    first.EditorPosition = {10.0F, 20.0F};
    first.Pins.push_back({Keire::AssetId::Generate(), "Out", Keire::ShaderGraphValueType::Scalar,
                          Keire::ShaderGraphPinDirection::Output, 0.0F});
    Keire::ShaderGraphNode second;
    second.Id = Keire::AssetId::Generate();
    second.Name = "Second";
    second.EditorPosition = {200.0F, 20.0F};
    second.Pins.push_back({Keire::AssetId::Generate(), "In", Keire::ShaderGraphValueType::Scalar,
                           Keire::ShaderGraphPinDirection::Input, 0.0F});
    definition.Nodes = {first, second};
    definition.Connections.push_back({Keire::AssetId::Generate(),
                                      {first.Id, first.Pins.front().Id},
                                      {second.Id, second.Pins.front().Id},
                                      {{110.0F, 15.0F}}});
    definition.Authoring.NodeAnnotations.push_back({first.Id, "Source", true, true});
    const auto commentId = Keire::AssetId::Generate();
    definition.Authoring.Comments.push_back({commentId,
                                             "Pair",
                                             {},
                                             {0.0F, 0.0F},
                                             {400.0F, 180.0F},
                                             {},
                                             18.0F,
                                             Keire::GraphCommentMoveMode::Group,
                                             {},
                                             {first.Id, second.Id},
                                             false});

    const std::array selection{first.Id, second.Id};
    const auto duplicated = KeireEditor::DuplicateShaderGraphSelection(definition, selection);

    REQUIRE(duplicated.size() == 2);
    CHECK(definition.Nodes.size() == 4);
    CHECK(definition.Connections.size() == 2);
    CHECK(definition.Authoring.NodeAnnotations.size() == 2);
    REQUIRE(definition.Authoring.Comments.size() == 2);
    const auto& copiedComment = definition.Authoring.Comments.back();
    CHECK(copiedComment.Id != commentId);
    CHECK(copiedComment.Position == Keire::Vector2{32.0F, 32.0F});
    CHECK(copiedComment.Members == duplicated);
    const auto& copiedConnection = definition.Connections.back();
    CHECK(copiedConnection.Output.Node == duplicated[0]);
    CHECK(copiedConnection.Input.Node == duplicated[1]);
    CHECK(copiedConnection.Id != definition.Connections.front().Id);
    REQUIRE(copiedConnection.RoutingPoints.size() == 1);
    CHECK(copiedConnection.RoutingPoints.front() == Keire::Vector2{142.0F, 47.0F});
    CHECK(definition.Connections.front().RoutingPoints.front() == Keire::Vector2{110.0F, 15.0F});
}

TEST_CASE("Shader and Material Graph duplication allocates resource-safe parameter symbols")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto parameter =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    parameter.Symbol = "Roughness";
    parameter.Name = "Surface Roughness";
    parameter.Value = 0.25F;
    parameter.ParameterMetadata.Description = "Keep author metadata";
    auto occupied =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    occupied.Symbol = "Roughness_Copy";
    graph.Nodes.push_back(parameter);
    graph.Nodes.push_back(occupied);
    graph.Resources.push_back({Keire::AssetId::Generate(), "Reserved sampler", "Roughness_Copy2",
                               Keire::ShaderGraphResourceKind::Sampler, Keire::SamplerDescription{}});
    const std::array selection{parameter.Id};

    SUBCASE("Shader Graph")
    {
        const auto first = KeireEditor::DuplicateShaderGraphSelection(graph, selection);
        const auto second = KeireEditor::DuplicateShaderGraphSelection(graph, selection);
        REQUIRE(first.size() == 1);
        REQUIRE(second.size() == 1);
        CHECK(graph.Nodes[3].Symbol == "Roughness_Copy3");
        CHECK(graph.Nodes[4].Symbol == "Roughness_Copy4");
    }
    SUBCASE("Material Graph surface expressions")
    {
        Keire::MaterialShaderReference shader;
        shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
        shader.Asset = Keire::AssetId::Generate();
        auto material = Keire::CreateMaterialGraph(shader, {});
        material.SurfaceGraph = graph;
        const auto first = KeireEditor::DuplicateMaterialGraphSelection(material, selection);
        const auto second = KeireEditor::DuplicateMaterialGraphSelection(material, selection);
        REQUIRE(first.size() == 1);
        REQUIRE(second.size() == 1);
        CHECK_NOTHROW(Keire::ValidateMaterialGraph(material));
        graph = material.SurfaceGraph;
        CHECK(graph.Nodes[3].Symbol == "Roughness_Copy3");
        CHECK(graph.Nodes[4].Symbol == "Roughness_Copy4");
    }
    CHECK(graph.Nodes[1] == parameter);
    CHECK(graph.Nodes.back().Name == parameter.Name);
    CHECK(graph.Nodes.back().Value == parameter.Value);
    CHECK(graph.Nodes.back().ParameterMetadata == parameter.ParameterMetadata);
    CHECK(graph.Nodes.back().Pins.front().Id != parameter.Pins.front().Id);
    CHECK_NOTHROW(Keire::ValidateShaderGraph(graph));
}

TEST_CASE("Graph duplication detaches a contained child from an unselected outer comment")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    const auto node = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant);
    graph.Nodes.push_back(node);
    Keire::GraphComment outer{.Id = Keire::AssetId::Generate(), .Title = "Outer"};
    Keire::GraphComment inner{
        .Id = Keire::AssetId::Generate(), .Title = "Inner", .Parent = outer.Id, .Members = {node.Id}};
    outer.Members = {inner.Id, graph.Nodes.front().Id};
    graph.Authoring.Comments = {outer, inner};
    REQUIRE_NOTHROW(Keire::ValidateShaderGraph(graph));
    const std::array selection{node.Id};

    const auto copied = KeireEditor::DuplicateShaderGraphSelection(graph, selection);

    REQUIRE(copied.size() == 1);
    REQUIRE(graph.Authoring.Comments.size() == 3);
    CHECK_FALSE(graph.Authoring.Comments.back().Parent);
    CHECK(graph.Authoring.Comments.back().Members == copied);
    CHECK(graph.Authoring.Comments[0] == outer);
    CHECK(graph.Authoring.Comments[1] == inner);
    CHECK_NOTHROW(Keire::ValidateShaderGraph(graph));
}

TEST_CASE("Graph duplication preserves mandatory output and VFX Context anchors")
{
    Keire::ShaderGraphDefinition shader;
    Keire::ShaderGraphNode output;
    output.Id = Keire::AssetId::Generate();
    output.Kind = Keire::ShaderGraphNodeKind::Master;
    shader.Nodes.push_back(output);
    const std::array shaderSelection{output.Id};
    CHECK(KeireEditor::DuplicateShaderGraphSelection(shader, shaderSelection).empty());
    CHECK(shader.Nodes.size() == 1);

    Keire::VfxEffectDefinition vfx;
    Keire::VfxGraphSystem system;
    system.Id = Keire::AssetId::Generate();
    Keire::VfxGraphNode context;
    context.Id = Keire::AssetId::Generate();
    context.Kind = Keire::VfxGraphNodeKind::Context;
    system.Nodes.push_back(context);
    vfx.Systems.push_back(system);
    const std::array vfxSelection{context.Id};
    CHECK(KeireEditor::DuplicateVfxGraphSelection(vfx, system.Id, vfxSelection).empty());
    CHECK(vfx.Systems.front().Nodes.size() == 1);
}

TEST_CASE("VFX graph duplication remaps internal pins and cables")
{
    Keire::VfxEffectDefinition definition;
    Keire::VfxGraphSystem system;
    system.Id = Keire::AssetId::Generate();
    Keire::VfxGraphNode first;
    first.Id = Keire::AssetId::Generate();
    first.Kind = Keire::VfxGraphNodeKind::Operator;
    first.Pins.push_back({Keire::AssetId::Generate(), "Out", Keire::VfxValueType::Scalar, false});
    Keire::VfxGraphNode second;
    second.Id = Keire::AssetId::Generate();
    second.Kind = Keire::VfxGraphNodeKind::Operator;
    second.Pins.push_back({Keire::AssetId::Generate(), "In", Keire::VfxValueType::Scalar, true});
    system.Nodes = {first, second};
    system.Connections.push_back(
        {Keire::AssetId::Generate(), first.Id, first.Pins.front().Id, second.Id, second.Pins.front().Id});
    system.Connections.back().RoutingPoints.push_back({100.0F, 50.0F});
    definition.Systems.push_back(system);

    const std::array selection{first.Id, second.Id};
    const auto duplicated = KeireEditor::DuplicateVfxGraphSelection(definition, system.Id, selection);

    REQUIRE(duplicated.size() == 2);
    REQUIRE(definition.Systems.front().Connections.size() == 2);
    const auto& copied = definition.Systems.front().Connections.back();
    CHECK(copied.OutputNode == duplicated[0]);
    CHECK(copied.InputNode == duplicated[1]);
    CHECK(copied.OutputPin != first.Pins.front().Id);
    CHECK(copied.InputPin != second.Pins.front().Id);
    REQUIRE(copied.RoutingPoints.size() == 1);
    CHECK(copied.RoutingPoints.front() == Keire::Vector2{132.0F, 82.0F});
}

TEST_CASE("Shader Graph extraction creates typed boundaries and rewires the parent through one function call")
{
    auto definition = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    auto constant =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    auto reroute =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Reroute, Keire::ShaderGraphValueType::Scalar);
    const auto outputNode = definition.Nodes.front().Id;
    const auto constantOutput =
        std::ranges::find(constant.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction)->Id;
    const auto rerouteInput =
        std::ranges::find(reroute.Pins, Keire::ShaderGraphPinDirection::Input, &Keire::ShaderGraphPin::Direction)->Id;
    const auto rerouteOutput =
        std::ranges::find(reroute.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction)->Id;
    const auto opacity = std::ranges::find(definition.Nodes.front().Pins, "Opacity", &Keire::ShaderGraphPin::Name)->Id;
    definition.Connections.push_back(
        {Keire::AssetId::Generate(), {constant.Id, constantOutput}, {reroute.Id, rerouteInput}});
    definition.Connections.push_back({Keire::AssetId::Generate(), {reroute.Id, rerouteOutput}, {outputNode, opacity}});
    definition.Nodes.push_back(constant);
    definition.Nodes.push_back(reroute);
    const std::array selection{reroute.Id};
    const auto functionAsset = Keire::AssetId::Generate();

    const auto extracted = KeireEditor::ExtractShaderGraphSelection(definition, selection, functionAsset, "Mask");

    CHECK_NOTHROW(Keire::ValidateGraphFunction(extracted.Function, Keire::ShaderGraphPurpose::ShaderFunction));
    CHECK_NOTHROW(Keire::ValidateShaderGraph(extracted.Parent));
    CHECK(std::ranges::find(extracted.Parent.Nodes, reroute.Id, &Keire::ShaderGraphNode::Id) ==
          extracted.Parent.Nodes.end());
    const auto call = std::ranges::find(extracted.Parent.Nodes, extracted.CallNode, &Keire::ShaderGraphNode::Id);
    REQUIRE(call != extracted.Parent.Nodes.end());
    CHECK(call->Kind == Keire::ShaderGraphNodeKind::FunctionCall);
    CHECK(call->ReferencedAsset == functionAsset);
    REQUIRE(extracted.Parent.Connections.size() == 2);
    CHECK(std::ranges::any_of(extracted.Parent.Connections, [&](const Keire::ShaderGraphConnection& connection)
                              { return connection.Input.Node == call->Id; }));
    CHECK(std::ranges::any_of(extracted.Parent.Connections, [&](const Keire::ShaderGraphConnection& connection)
                              { return connection.Output.Node == call->Id; }));
}

TEST_CASE("Material Graph extraction accepts expression selections and rejects value bindings")
{
    Keire::MaterialShaderReference shader;
    shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    shader.Asset = Keire::AssetId::Generate();
    Keire::ShaderInterfaceDefinition shaderInterface;
    Keire::ShaderPropertyDefinition property;
    property.Id = Keire::AssetId::Generate();
    property.Name = "Roughness";
    property.Type = Keire::ShaderPropertyType::Scalar;
    property.DefaultValue.X = 0.5F;
    shaderInterface.Properties.push_back(property);
    auto definition = Keire::CreateMaterialGraph(shader, shaderInterface);
    auto constant =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    auto reroute =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Reroute, Keire::ShaderGraphValueType::Scalar);
    const auto constantOutput =
        std::ranges::find(constant.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction)->Id;
    const auto rerouteInput =
        std::ranges::find(reroute.Pins, Keire::ShaderGraphPinDirection::Input, &Keire::ShaderGraphPin::Direction)->Id;
    const auto rerouteOutput =
        std::ranges::find(reroute.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction)->Id;
    const auto output = definition.SurfaceGraph.Nodes.front().Id;
    const auto opacity =
        std::ranges::find(definition.SurfaceGraph.Nodes.front().Pins, "Opacity", &Keire::ShaderGraphPin::Name)->Id;
    definition.SurfaceGraph.Nodes.push_back(constant);
    definition.SurfaceGraph.Nodes.push_back(reroute);
    definition.SurfaceGraph.Connections.push_back(
        {Keire::AssetId::Generate(), {constant.Id, constantOutput}, {reroute.Id, rerouteInput}});
    definition.SurfaceGraph.Connections.push_back(
        {Keire::AssetId::Generate(), {reroute.Id, rerouteOutput}, {output, opacity}});
    definition.Nodes.push_back(Keire::CreateMaterialGraphValueNode(Keire::ShaderPropertyType::Scalar, 0.25F));
    const std::array selection{reroute.Id};

    const auto extracted =
        KeireEditor::ExtractMaterialGraphSelection(definition, selection, Keire::AssetId::Generate(), "Mask");

    CHECK_NOTHROW(Keire::ValidateGraphFunction(extracted.Function, Keire::ShaderGraphPurpose::MaterialFunction));
    CHECK_NOTHROW(Keire::ValidateMaterialGraph(extracted.Parent));
    const std::array valueSelection{definition.Nodes.front().Id};
    CHECK_THROWS_AS((void)KeireEditor::ExtractMaterialGraphSelection(definition, valueSelection,
                                                                     Keire::AssetId::Generate(), "Invalid"),
                    std::invalid_argument);
}

TEST_CASE("Material Graph extraction gives multi-node boundary inputs symbols distinct from graph resources")
{
    Keire::MaterialShaderReference shader;
    shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
    shader.Asset = Keire::AssetId::Generate();
    auto definition = Keire::CreateMaterialGraph(shader, {});
    auto constant =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    auto add = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Add, Keire::ShaderGraphValueType::Scalar);
    auto reroute =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Reroute, Keire::ShaderGraphValueType::Scalar);
    const auto outputPin = [](const Keire::ShaderGraphNode& node)
    {
        return std::ranges::find(node.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction)
            ->Id;
    };
    const auto inputPin = [](const Keire::ShaderGraphNode& node, const std::string_view name)
    { return std::ranges::find(node.Pins, name, &Keire::ShaderGraphPin::Name)->Id; };
    const auto output = definition.SurfaceGraph.Nodes.front().Id;
    const auto opacity = inputPin(definition.SurfaceGraph.Nodes.front(), "Opacity");
    definition.SurfaceGraph.Resources.push_back(
        {Keire::AssetId::Generate(), "A", "A", Keire::ShaderGraphResourceKind::Sampler, Keire::SamplerDescription{}});
    definition.SurfaceGraph.Nodes.push_back(constant);
    definition.SurfaceGraph.Nodes.push_back(add);
    definition.SurfaceGraph.Nodes.push_back(reroute);
    definition.SurfaceGraph.Connections.push_back(
        {Keire::AssetId::Generate(), {constant.Id, outputPin(constant)}, {add.Id, inputPin(add, "A")}});
    definition.SurfaceGraph.Connections.push_back(
        {Keire::AssetId::Generate(), {add.Id, outputPin(add)}, {reroute.Id, inputPin(reroute, "Input")}});
    definition.SurfaceGraph.Connections.push_back(
        {Keire::AssetId::Generate(), {reroute.Id, outputPin(reroute)}, {output, opacity}});
    const std::array selection{add.Id, reroute.Id};

    const auto extracted =
        KeireEditor::ExtractMaterialGraphSelection(definition, selection, Keire::AssetId::Generate(), "Combined");

    CHECK_NOTHROW(Keire::ValidateGraphFunction(extracted.Function, Keire::ShaderGraphPurpose::MaterialFunction));
    const auto parameter = std::ranges::find(extracted.Function.Body.Nodes, Keire::ShaderGraphNodeKind::Parameter,
                                             &Keire::ShaderGraphNode::Kind);
    REQUIRE(parameter != extracted.Function.Body.Nodes.end());
    CHECK(parameter->Symbol == "A2");
    CHECK_NOTHROW(Keire::ValidateMaterialGraph(extracted.Parent));
}
