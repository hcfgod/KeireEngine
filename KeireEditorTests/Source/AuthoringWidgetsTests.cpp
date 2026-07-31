#include "KeireClient/Editor/AuthoringWidgets.h"

#include <doctest/doctest.h>

#include <stdexcept>
#include <vector>

TEST_CASE("Stable node graph validation protects local identity and references")
{
    const std::vector<KeireEditor::NodeGraphNode> nodes{{1, "Entry", {0.0F, 0.0F}}, {2, "Locomotion", {200.0F, 0.0F}}};
    const std::vector<KeireEditor::NodeGraphConnection> connections{{3, 1, 2, "Enter"}};
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(nodes, connections));

    auto duplicateNodes = nodes;
    duplicateNodes.push_back(nodes.front());
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(duplicateNodes, connections), std::invalid_argument);

    auto missingTarget = connections;
    missingTarget.front().Target = 99;
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(nodes, missingTarget), std::invalid_argument);

    const std::vector<KeireEditor::NodeGraphNode> loopNode{{4, "Feedback", {0.0F, 0.0F}}};
    const std::vector<KeireEditor::NodeGraphConnection> loop{{5, 4, 4, "Feedback"}};
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(loopNode, loop));
}

TEST_CASE("Stable node graph local IDs remap zero and collisions without losing source identity")
{
    KeireEditor::StableNodeGraphIdMap ids;
    const Keire::AssetId first(1, 2);
    const Keire::AssetId second(3, 4);
    const auto firstCanvasId = ids.Assign(first, 0);
    const auto secondCanvasId = ids.Assign(second, firstCanvasId);

    CHECK(firstCanvasId != 0);
    CHECK(secondCanvasId != 0);
    CHECK(secondCanvasId != firstCanvasId);
    CHECK(ids.Assign(first, 99) == firstCanvasId);
    CHECK(ids.Find(first) == firstCanvasId);
    CHECK_FALSE(ids.Find(Keire::AssetId(5, 6)).has_value());
}

TEST_CASE("Stable node graph presentation metadata remains optional")
{
    const KeireEditor::NodeGraphNode legacyNode{1, "Legacy", {0.0F, 0.0F}};
    CHECK(legacyNode.Subtitle.empty());

    auto contextualNode = legacyNode;
    contextualNode.Subtitle = "Update Context";
    CHECK(contextualNode != legacyNode);
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(std::span{&contextualNode, std::size_t{1}}, {}));

    const KeireEditor::NodeGraphCanvasResult legacyResult{std::nullopt, std::nullopt, true, true};
    CHECK(legacyResult.BackgroundActivated);
    CHECK(legacyResult.Changed);
    CHECK_FALSE(legacyResult.MoveCompletedNode.has_value());
}

TEST_CASE("Stable node graph focus deterministically frames authored nodes")
{
    const std::vector<KeireEditor::NodeGraphNode> nodes{{41, "First", {100.0F, 50.0F}, {100.0F, 50.0F}},
                                                        {42, "Second", {400.0F, 250.0F}, {100.0F, 50.0F}}};
    KeireEditor::StableNodeGraphCanvas canvas;
    canvas.Focus(nodes, {800.0F, 600.0F});
    CHECK(canvas.Zoom() > 0.0F);
    const auto firstPan = canvas.Pan();
    canvas.Focus(nodes, {800.0F, 600.0F});
    CHECK(canvas.Pan() == firstPan);
}
