#include "KeireClient/Editor/GraphNavigation.h"

#include <doctest/doctest.h>

TEST_CASE("Graph diagnostics resolve pin-only locations to their authored node")
{
    const auto shader = Keire::CreateDefaultShaderGraph();
    const auto& shaderNode = shader.Nodes.front();
    const auto shaderPin = shaderNode.Pins.front().Id;
    CHECK(KeireEditor::ResolveShaderGraphDiagnosticNode(
              shader, {.Code = "SGTEST", .Message = "Pin failure", .Pin = shaderPin}) == shaderNode.Id);

    const auto material = Keire::CreateOpenPbrMaterial();
    const auto& materialNode = material.SurfaceGraph.Nodes.front();
    const auto materialPin = materialNode.Pins.front().Id;
    CHECK(KeireEditor::ResolveMaterialGraphDiagnosticNode(
              material, {.Code = "MATTEST", .Message = "Pin failure", .Pin = materialPin}) == materialNode.Id);

    CHECK_FALSE(KeireEditor::ResolveShaderGraphDiagnosticNode(
        shader, {.Code = "SGTEST", .Message = "Missing pin", .Pin = Keire::AssetId::Generate()}));
}
