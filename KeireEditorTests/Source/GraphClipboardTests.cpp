#include "KeireClient/Editor/GraphClipboard.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <string>

namespace
{
    [[nodiscard]] Keire::MaterialGraphDefinition MaterialGraph()
    {
        Keire::MaterialShaderReference shader;
        shader.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
        shader.Asset = Keire::AssetId::Generate();
        return Keire::CreateMaterialGraph(shader, {});
    }

    [[nodiscard]] Keire::ShaderGraphNode SurfaceParameter(const std::string& symbol)
    {
        auto result =
            Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
        result.Symbol = symbol;
        return result;
    }
} // namespace

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

TEST_CASE("Shader Graph clipboard gives pasted parameters deterministic unique symbols")
{
    auto source = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    auto parameter =
        Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter, Keire::ShaderGraphValueType::Scalar);
    parameter.Symbol = "SurfaceRoughness";
    parameter.Name = "Surface Roughness";
    parameter.ParameterMetadata.Description = "Preserved clipboard metadata";
    source.Nodes.push_back(parameter);
    const std::array selection{parameter.Id};
    const auto fragment = KeireEditor::CopyShaderGraphFragment(source, selection);

    auto target = Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Unlit);
    auto existing = parameter;
    existing.Id = Keire::AssetId::Generate();
    for (auto& pin : existing.Pins)
        pin.Id = Keire::AssetId::Generate();
    target.Nodes.push_back(existing);

    const auto first = KeireEditor::PasteShaderGraphFragment(target, fragment);
    const auto second = KeireEditor::PasteShaderGraphFragment(target, fragment);

    REQUIRE(first.size() == 1);
    REQUIRE(second.size() == 1);
    const auto firstNode = std::ranges::find(target.Nodes, first.front(), &Keire::ShaderGraphNode::Id);
    const auto secondNode = std::ranges::find(target.Nodes, second.front(), &Keire::ShaderGraphNode::Id);
    REQUIRE(firstNode != target.Nodes.end());
    REQUIRE(secondNode != target.Nodes.end());
    CHECK(firstNode->Symbol == "SurfaceRoughness_Copy");
    CHECK(secondNode->Symbol == "SurfaceRoughness_Copy2");
    CHECK(firstNode->Name == parameter.Name);
    CHECK(firstNode->ParameterMetadata == parameter.ParameterMetadata);
    CHECK_NOTHROW(Keire::ValidateShaderGraph(target));
}

TEST_CASE("Material and VFX clipboard fragments remap editor topology across documents")
{
    auto materialSource = MaterialGraph();
    auto materialFirst = Keire::CreateMaterialGraphValueNode(Keire::ShaderPropertyType::Scalar, 0.25F, {20.0F, 30.0F});
    auto materialSecond = Keire::CreateMaterialGraphValueNode(Keire::ShaderPropertyType::Color,
                                                              Keire::Color{1.0F, 1.0F, 1.0F, 1.0F}, {80.0F, 90.0F});
    materialSource.Nodes.push_back(materialFirst);
    materialSource.Nodes.push_back(materialSecond);
    const std::array materialSelection{materialFirst.Id, materialSecond.Id};

    const auto materialFragment = KeireEditor::CopyMaterialGraphFragment(materialSource, materialSelection);
    auto materialTarget = MaterialGraph();
    const auto materialPasted = KeireEditor::PasteMaterialGraphFragment(materialTarget, materialFragment);

    REQUIRE(materialPasted.size() == 2);
    CHECK(materialPasted[0] != materialFirst.Id);
    CHECK(materialPasted[1] != materialSecond.Id);
    REQUIRE(materialTarget.Nodes.size() == 2);
    CHECK(materialTarget.Nodes[0].Id == materialPasted[0]);
    CHECK(materialTarget.Nodes[1].Id == materialPasted[1]);
    CHECK(materialTarget.Nodes[0].Value == materialFirst.Value);
    CHECK(materialTarget.Nodes[1].Value == materialSecond.Value);

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

TEST_CASE("Material Graph clipboard gives pasted surface parameters deterministic resource-safe symbols")
{
    auto source = MaterialGraph();
    const auto parameter = SurfaceParameter("SurfaceTint");
    source.SurfaceGraph.Nodes.push_back(parameter);
    const std::array selection{parameter.Id};
    const auto fragment = KeireEditor::CopyMaterialGraphFragment(source, selection);

    auto target = MaterialGraph();
    target.SurfaceGraph.Resources.push_back({Keire::AssetId::Generate(), "Surface Tint Resource", "SurfaceTint",
                                             Keire::ShaderGraphResourceKind::Sampler, Keire::SamplerDescription{}});
    target.SurfaceGraph.Nodes.push_back(SurfaceParameter("SurfaceTint_Copy"));
    REQUIRE_NOTHROW(Keire::ValidateMaterialGraph(target));

    const auto first = KeireEditor::PasteMaterialGraphFragment(target, fragment);
    const auto second = KeireEditor::PasteMaterialGraphFragment(target, fragment);

    REQUIRE(first.size() == 1);
    REQUIRE(second.size() == 1);
    const auto firstNode = std::ranges::find(target.SurfaceGraph.Nodes, first.front(), &Keire::ShaderGraphNode::Id);
    const auto secondNode = std::ranges::find(target.SurfaceGraph.Nodes, second.front(), &Keire::ShaderGraphNode::Id);
    REQUIRE(firstNode != target.SurfaceGraph.Nodes.end());
    REQUIRE(secondNode != target.SurfaceGraph.Nodes.end());
    CHECK(firstNode->Symbol == "SurfaceTint_Copy2");
    CHECK(secondNode->Symbol == "SurfaceTint_Copy3");
    CHECK_NOTHROW(Keire::ValidateMaterialGraph(target));
}

TEST_CASE("Material Graph clipboard validates the complete candidate before committing")
{
    auto source = MaterialGraph();
    const auto parameter = SurfaceParameter("AdditionalParameter");
    source.SurfaceGraph.Nodes.push_back(parameter);
    const std::array selection{parameter.Id};
    const auto fragment = KeireEditor::CopyMaterialGraphFragment(source, selection);

    auto target = MaterialGraph();
    for (std::size_t index = 0; index < 80U; ++index)
        target.SurfaceGraph.Nodes.push_back(SurfaceParameter("ExistingParameter" + std::to_string(index)));
    REQUIRE_NOTHROW(Keire::ValidateMaterialGraph(target));
    const auto original = target;

    CHECK_THROWS_AS((void)KeireEditor::PasteMaterialGraphFragment(target, fragment), std::invalid_argument);
    CHECK(target == original);
}
