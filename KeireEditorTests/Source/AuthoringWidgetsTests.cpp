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
