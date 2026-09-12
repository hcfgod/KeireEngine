#include "KeireClient/Editor/ShaderGraphBlackboard.h"
#include "KeireClient/Editor/ShaderGraphDocument.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>

TEST_CASE("Shader Graph blackboard groups properties by category and explicit priority")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto later = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter);
    later.Name = "Alpha";
    later.Symbol = "Later";
    later.ParameterMetadata.Category = "Surface";
    later.ParameterMetadata.SortPriority = 10;
    auto earlier = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter);
    earlier.Name = "Zeta";
    earlier.Symbol = "Earlier";
    earlier.ParameterMetadata.Category = "Surface";
    earlier.ParameterMetadata.SortPriority = -5;
    auto defaultCategory = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter);
    graph.Nodes.insert(graph.Nodes.end(), {later, earlier, defaultCategory});
    const auto before = graph;
    const auto entries = KeireEditor::BuildShaderGraphBlackboard(graph);
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].Category == "Properties");
    CHECK(entries[1].Node == earlier.Id);
    CHECK(entries[2].Node == later.Id);
    CHECK(graph == before);
}

TEST_CASE("Shader Graph blackboard searches names symbols categories and descriptions without losing stable IDs")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    auto first = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter);
    first.Name = "Shared name";
    first.Symbol = "Roughness";
    first.ParameterMetadata.Category = "Surface";
    first.ParameterMetadata.Description = "Microsurface scattering";
    auto second = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Parameter);
    second.Name = first.Name;
    second.Symbol = "Metallic";
    graph.Nodes.insert(graph.Nodes.end(), {first, second});
    const auto entries = KeireEditor::BuildShaderGraphBlackboard(graph, "SHARED NAME");
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].Identity != entries[1].Identity);
    for (const auto query : {"roughness", "SURFACE", "scattering"})
    {
        const auto filtered = KeireEditor::BuildShaderGraphBlackboard(graph, query);
        REQUIRE(filtered.size() == 1);
        CHECK(filtered.front().Node == first.Id);
    }
    const auto originalIdentity = KeireEditor::BuildShaderGraphBlackboard(graph, "roughness").front().Identity;
    graph.Nodes[1].Name = "Renamed property";
    CHECK(KeireEditor::BuildShaderGraphBlackboard(graph, "roughness").front().Identity == originalIdentity);
    CHECK(KeireEditor::BuildShaderGraphBlackboard(graph, "missing").empty());
}

TEST_CASE("Shader Graph blackboard includes hidden and unplaced keyword declarations once")
{
    auto graph = Keire::CreateDefaultShaderGraph();
    graph.Keywords = {{.Name = "FEATURE", .DefaultOption = "true", .Exposed = false},
                      {.Name = "QUALITY", .Options = {"Low", "High"}, .DefaultOption = "High"}};
    auto keyword = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Keyword);
    keyword.Symbol = "FEATURE";
    auto duplicateReference = Keire::CreateShaderGraphNode(Keire::ShaderGraphNodeKind::Keyword);
    duplicateReference.Symbol = keyword.Symbol;
    graph.Nodes.insert(graph.Nodes.end(), {keyword, duplicateReference});
    const auto entries = KeireEditor::BuildShaderGraphBlackboard(graph);
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].Keyword);
    CHECK_FALSE(entries[0].Exposed);
    CHECK(entries[0].Node == std::min(keyword.Id, duplicateReference.Id));
    CHECK(entries[0].Description == "Default: true");
    CHECK(entries[1].Keyword);
    CHECK(entries[1].Exposed);
    CHECK_FALSE(entries[1].Node);
    CHECK(entries[1].Symbol == "QUALITY_High");
    CHECK(entries[1].Description == "Default: High");
    CHECK(entries[2].Symbol == "QUALITY_Low");
    CHECK(KeireEditor::ShaderGraphHasKeywordToken(graph, "FEATURE"));
    CHECK(KeireEditor::ShaderGraphHasKeywordToken(graph, "QUALITY_High"));
    CHECK_FALSE(KeireEditor::ShaderGraphHasKeywordToken(graph, "QUALITY"));
    CHECK_FALSE(KeireEditor::ShaderGraphHasKeywordToken(graph, "QUALITY_Unknown"));
}

TEST_CASE("Shader Graph frame all fits the actual narrow canvas below compact toolbar rows")
{
    KeireEditor::ShaderGraphDocument document({.Persist = [](Keire::AssetId, std::span<const std::byte>) {}});
    document.Open(Keire::AssetId::Generate(), Keire::CreateDefaultShaderGraph(Keire::ShaderGraphOutput::Fullscreen), 1);
    const auto model = document.BuildCanvasModel();
    REQUIRE(model.Nodes.size() == 1);
    const auto& node = model.Nodes.front();
    const auto inputs =
        std::ranges::count(node.Pins, KeireEditor::NodeGraphPinDirection::Input, &KeireEditor::NodeGraphPin::Direction);
    REQUIRE(inputs == 5);
    const float renderedHeight = std::max(node.Size.Y, 48.0F + static_cast<float>(inputs) * 22.0F + 8.0F);
    KeireEditor::StableNodeGraphCanvas canvas;
    for (const auto viewport :
         std::array{Keire::UiSize{430.0F, 217.0F}, Keire::UiSize{900.0F, 430.0F}, Keire::UiSize{430.0F, 217.0F}})
    {
        canvas.Focus(model.Nodes, viewport);
        const auto offset = canvas.Pan();
        const auto scale = canvas.Zoom();
        CHECK((node.Position.X + offset.X) * scale >= 0.0F);
        CHECK((node.Position.Y + offset.Y) * scale >= 0.0F);
        CHECK((node.Position.X + offset.X + node.Size.X) * scale <= viewport.Width);
        CHECK((node.Position.Y + offset.Y + renderedHeight) * scale <= viewport.Height);
    }
}
