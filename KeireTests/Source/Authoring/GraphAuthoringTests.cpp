#include "Keire/Authoring/GraphAuthoring.h"

#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

TEST_CASE("graph authoring metadata validates nested ownership and annotations")
{
    const std::array nodes{Keire::AssetId::Generate(), Keire::AssetId::Generate()};
    const auto outer = Keire::AssetId::Generate();
    const auto inner = Keire::AssetId::Generate();
    Keire::GraphAuthoringMetadata metadata;
    metadata.NodeAnnotations.push_back({nodes[0], "Pinned note", true, true});
    metadata.Comments.push_back({outer,
                                 "Outer",
                                 "A movable region.",
                                 {0.0F, 0.0F},
                                 {640.0F, 480.0F},
                                 {0.1F, 0.3F, 0.7F, 0.4F},
                                 20.0F,
                                 Keire::GraphCommentMoveMode::Group,
                                 {},
                                 {nodes[0], inner},
                                 false});
    metadata.Comments.push_back({inner,
                                 "Inner",
                                 "A nested collapsed region.",
                                 {40.0F, 80.0F},
                                 {320.0F, 220.0F},
                                 {0.7F, 0.3F, 0.1F, 0.35F},
                                 16.0F,
                                 Keire::GraphCommentMoveMode::CommentOnly,
                                 outer,
                                 {nodes[1]},
                                 true});

    CHECK_NOTHROW(Keire::ValidateGraphAuthoringMetadata(metadata, nodes));

    auto cycle = metadata;
    cycle.Comments.front().Parent = inner;
    cycle.Comments.back().Members.push_back(outer);
    CHECK_THROWS_AS(Keire::ValidateGraphAuthoringMetadata(cycle, nodes), std::invalid_argument);

    auto duplicateOwner = metadata;
    duplicateOwner.Comments.front().Members.push_back(nodes[1]);
    CHECK_THROWS_AS(Keire::ValidateGraphAuthoringMetadata(duplicateOwner, nodes), std::invalid_argument);

    auto unknownNode = metadata;
    unknownNode.NodeAnnotations.front().Node = Keire::AssetId::Generate();
    CHECK_THROWS_AS(Keire::ValidateGraphAuthoringMetadata(unknownNode, nodes), std::invalid_argument);
}

TEST_CASE("graph comment membership follows the smallest containing region")
{
    const auto node = Keire::AssetId::Generate();
    Keire::GraphAuthoringMetadata metadata;
    metadata.Comments.push_back({Keire::AssetId::Generate(), "Outer", {}, {0.0F, 0.0F}, {800.0F, 600.0F}});
    metadata.Comments.push_back({Keire::AssetId::Generate(), "Inner", {}, {100.0F, 100.0F}, {300.0F, 260.0F}});

    Keire::UpdateGraphCommentMembership(metadata, node, {140.0F, 150.0F}, {100.0F, 80.0F});
    CHECK(metadata.Comments.front().Members.empty());
    REQUIRE(metadata.Comments.back().Members.size() == 1);
    CHECK(metadata.Comments.back().Members.front() == node);

    Keire::UpdateGraphCommentMembership(metadata, node, {900.0F, 800.0F});
    CHECK(metadata.Comments.front().Members.empty());
    CHECK(metadata.Comments.back().Members.empty());
}

TEST_CASE("deleting a graph comment preserves and reparents its contents")
{
    const auto node = Keire::AssetId::Generate();
    const auto outer = Keire::AssetId::Generate();
    const auto inner = Keire::AssetId::Generate();
    Keire::GraphAuthoringMetadata metadata;
    metadata.Comments.push_back(
        {outer, "Outer", {}, {}, {640.0F, 480.0F}, {}, 18.0F, Keire::GraphCommentMoveMode::Group, {}, {inner}});
    metadata.Comments.push_back(
        {inner, "Inner", {}, {}, {320.0F, 220.0F}, {}, 18.0F, Keire::GraphCommentMoveMode::Group, outer, {node}});

    Keire::RemoveGraphComment(metadata, inner);
    REQUIRE(metadata.Comments.size() == 1);
    REQUIRE(metadata.Comments.front().Members.size() == 1);
    CHECK(metadata.Comments.front().Members.front() == node);
}
