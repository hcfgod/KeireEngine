#include "KeireClient/Editor/GraphLayout.h"

#include <doctest/doctest.h>

#include <array>

TEST_CASE("Graph alignment uses node bounds and preserves the unaligned axis")
{
    const std::array nodes{
        KeireEditor::NodeGraphNode{.Id = 1, .Position = {20.0F, 10.0F}, .Size = {100.0F, 40.0F}},
        KeireEditor::NodeGraphNode{.Id = 2, .Position = {200.0F, 80.0F}, .Size = {60.0F, 80.0F}},
    };
    const std::array<KeireEditor::StableNodeId, 2> selection{1, 2};

    const auto right = KeireEditor::AlignGraphNodes(nodes, selection, KeireEditor::GraphAlignment::Right);

    REQUIRE(right.size() == 2);
    CHECK(right[0].second == Keire::Vector2{160.0F, 10.0F});
    CHECK(right[1].second == Keire::Vector2{200.0F, 80.0F});
}

TEST_CASE("Graph distribution is stable by position and retains endpoints")
{
    const std::array nodes{
        KeireEditor::NodeGraphNode{.Id = 3, .Position = {300.0F, 25.0F}},
        KeireEditor::NodeGraphNode{.Id = 1, .Position = {0.0F, 10.0F}},
        KeireEditor::NodeGraphNode{.Id = 2, .Position = {90.0F, 15.0F}},
    };
    const std::array<KeireEditor::StableNodeId, 3> selection{3, 1, 2};

    const auto distributed =
        KeireEditor::DistributeGraphNodes(nodes, selection, KeireEditor::GraphDistribution::Horizontal);

    REQUIRE(distributed.size() == 3);
    CHECK(distributed[0] == std::pair<KeireEditor::StableNodeId, Keire::Vector2>{1, {0.0F, 10.0F}});
    CHECK(distributed[1] == std::pair<KeireEditor::StableNodeId, Keire::Vector2>{2, {150.0F, 15.0F}});
    CHECK(distributed[2] == std::pair<KeireEditor::StableNodeId, Keire::Vector2>{3, {300.0F, 25.0F}});
}

TEST_CASE("Straighten selection finds only fully internal cables")
{
    const std::array connections{
        KeireEditor::NodeGraphConnection{.Id = 10, .Source = 1, .Target = 2},
        KeireEditor::NodeGraphConnection{.Id = 11, .Source = 2, .Target = 3},
    };
    const std::array<KeireEditor::StableNodeId, 2> selection{1, 2};

    CHECK(KeireEditor::InternalGraphConnections(connections, selection) == std::vector<KeireEditor::StableNodeId>{10});
}
