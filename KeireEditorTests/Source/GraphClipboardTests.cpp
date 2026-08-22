#include "KeireClient/Editor/GraphClipboard.h"

#include <doctest/doctest.h>

#include <array>
#include <string>

TEST_CASE("Shader Graph clipboard is canonical versioned JSON and remaps every topology identity")
{
    auto source = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    auto first =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    auto second =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Reroute, Keire::ShaderGraphValueType::Scalar);
    first.EditorPosition = {10.0F, 20.0F};
    second.EditorPosition = {180.0F, 20.0F};
    const auto output =
        std::ranges::find(first.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction)->Id;
    const auto input =
        std::ranges::find(second.Pins, Keire::ShaderGraphPinDirection::Input, &Keire::ShaderGraphPin::Direction)->Id;
    source.Connections.push_back({Keire::AssetId::Generate(), {first.Id, output}, {second.Id, input}});
    source.Nodes.push_back(first);
    source.Nodes.push_back(second);
    source.Authoring.NodeAnnotations.push_back({first.Id, "Copied", true, true});
    const std::array selection{first.Id, second.Id};

    const auto fragment = KeireEditor::CopyShaderGraphFragment(source, selection);
    auto target = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    const auto pasted = KeireEditor::PasteShaderGraphFragment(target, fragment);

    CHECK(fragment.starts_with("{\"format\":\"keire.graph-fragment\""));
    REQUIRE(pasted.size() == 2);
    CHECK(pasted[0] != first.Id);
    CHECK(pasted[1] != second.Id);
    REQUIRE(target.Connections.size() == 1);
    CHECK(target.Connections.front().Output.Node == pasted[0]);
    CHECK(target.Connections.front().Input.Node == pasted[1]);
    REQUIRE(target.Authoring.NodeAnnotations.size() == 1);
    CHECK(target.Authoring.NodeAnnotations.front().Node == pasted[0]);
}

TEST_CASE("Graph clipboard rejects malformed wrong-kind and oversized input before mutation")
{
    auto target = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    const auto original = target;

    CHECK_THROWS_AS((void)KeireEditor::PasteShaderGraphFragment(target, "not json"), std::invalid_argument);
    CHECK(target == original);
    CHECK_THROWS_WITH_AS((void)KeireEditor::PasteShaderGraphFragment(
                             target, "{\"format\":\"keire.graph-fragment\",\"version\":1,\"kind\":\"vfx\","
                                     "\"selection\":[\"00000000-0000-0000-0000-000000000001\"],\"source\":{}}"),
                         "Graph fragment belongs to a different graph editor.", std::invalid_argument);
    CHECK(target == original);
    const std::string oversized(KeireEditor::MaximumGraphFragmentBytes + 1U, 'x');
    CHECK_THROWS_AS((void)KeireEditor::PasteShaderGraphFragment(target, oversized), std::invalid_argument);
    CHECK(target == original);
}

TEST_CASE("Material and VFX clipboard fragments remap editor topology across documents")
{
    Keire::MaterialGraphDefinition materialSource;
    materialSource.Shader.Asset = Keire::AssetId::Generate();
    materialSource.OutputNode = Keire::AssetId::Generate();
    materialSource.SurfaceGraph = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    auto materialFirst =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Constant, Keire::ShaderGraphValueType::Scalar);
    auto materialSecond =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Reroute, Keire::ShaderGraphValueType::Scalar);
    const auto materialOutput =
        std::ranges::find(materialFirst.Pins, Keire::ShaderGraphPinDirection::Output, &Keire::ShaderGraphPin::Direction)
            ->Id;
    const auto materialInput =
        std::ranges::find(materialSecond.Pins, Keire::ShaderGraphPinDirection::Input, &Keire::ShaderGraphPin::Direction)
            ->Id;
    materialSource.SurfaceGraph.Nodes.push_back(materialFirst);
    materialSource.SurfaceGraph.Nodes.push_back(materialSecond);
    materialSource.SurfaceGraph.Connections.push_back(
        {Keire::AssetId::Generate(), {materialFirst.Id, materialOutput}, {materialSecond.Id, materialInput}});
    const std::array materialSelection{materialFirst.Id, materialSecond.Id};

    const auto materialFragment = KeireEditor::CopyMaterialGraphFragment(materialSource, materialSelection);
    Keire::MaterialGraphDefinition materialTarget;
    materialTarget.Shader.Asset = Keire::AssetId::Generate();
    materialTarget.OutputNode = Keire::AssetId::Generate();
    const auto materialPasted = KeireEditor::PasteMaterialGraphFragment(materialTarget, materialFragment);

    REQUIRE(materialPasted.size() == 2);
    CHECK(materialPasted[0] != materialFirst.Id);
    CHECK(materialPasted[1] != materialSecond.Id);
    REQUIRE(materialTarget.SurfaceGraph.Connections.size() == 1);
    CHECK(materialTarget.SurfaceGraph.Connections.front().Output.Node == materialPasted[0]);
    CHECK(materialTarget.SurfaceGraph.Connections.front().Input.Node == materialPasted[1]);

    Keire::VfxEffectDefinition vfxSource;
    vfxSource.EmitterId = Keire::AssetId::Generate();
    vfxSource.Name = "Clipboard Source";
    vfxSource.Modules.push_back({Keire::AssetId::Generate(), true, Keire::VfxEmissionRateModule{}});
    vfxSource.Modules.push_back(
        {Keire::AssetId::Generate(), true,
         Keire::VfxRendererModule{Keire::VfxRendererType::Sprite, Keire::AssetId::Generate(), {}}});
    Keire::VfxGraphSystem vfxSystem;
    vfxSystem.Id = Keire::AssetId::Generate();
    vfxSystem.Name = "System";
    auto vfxFirst = Keire::CreateVfxGraphOperatorNode("keire.operator.add");
    auto vfxSecond = Keire::CreateVfxGraphOperatorNode("keire.operator.add");
    const auto vfxOutput = std::ranges::find(vfxFirst.Pins, false, &Keire::VfxGraphPin::Input)->Id;
    const auto vfxInput = std::ranges::find(vfxSecond.Pins, true, &Keire::VfxGraphPin::Input)->Id;
    vfxSystem.Nodes = {vfxFirst, vfxSecond};
    vfxSystem.Connections.push_back({Keire::AssetId::Generate(), vfxFirst.Id, vfxOutput, vfxSecond.Id, vfxInput});
    vfxSource.Systems.push_back(vfxSystem);
    const std::array vfxSelection{vfxFirst.Id, vfxSecond.Id};

    const auto vfxFragment = KeireEditor::CopyVfxGraphFragment(vfxSource, vfxSystem.Id, vfxSelection);
    Keire::VfxEffectDefinition vfxTarget;
    vfxTarget.EmitterId = Keire::AssetId::Generate();
    vfxTarget.Name = "Clipboard Target";
    Keire::VfxGraphSystem targetSystem;
    targetSystem.Id = Keire::AssetId::Generate();
    const auto targetSystemId = targetSystem.Id;
    vfxTarget.Systems.push_back(targetSystem);
    const auto vfxPasted = KeireEditor::PasteVfxGraphFragment(vfxTarget, targetSystemId, vfxFragment);

    REQUIRE(vfxPasted.size() == 2);
    CHECK(vfxPasted[0] != vfxFirst.Id);
    CHECK(vfxPasted[1] != vfxSecond.Id);
    REQUIRE(vfxTarget.Systems.front().Connections.size() == 1);
    CHECK(vfxTarget.Systems.front().Connections.front().OutputNode == vfxPasted[0]);
    CHECK(vfxTarget.Systems.front().Connections.front().InputNode == vfxPasted[1]);
}
