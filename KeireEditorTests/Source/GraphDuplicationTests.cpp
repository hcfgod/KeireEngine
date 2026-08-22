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
    definition.Connections.push_back(
        {Keire::AssetId::Generate(), {first.Id, first.Pins.front().Id}, {second.Id, second.Pins.front().Id}, {}});
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
}
