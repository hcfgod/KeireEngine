#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Math/Math.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    enum class GraphCommentMoveMode : std::uint8_t
    {
        Group,
        CommentOnly
    };

    struct GraphNodeAnnotation
    {
        AssetId Node;
        std::string Text;
        bool Visible = true;
        bool Pinned = false;

        bool operator==(const GraphNodeAnnotation&) const = default;
    };

    /// Editor-only graph region. Members contain node or nested-comment identities and never affect compilation.
    struct GraphComment
    {
        AssetId Id;
        std::string Title = "Comment";
        std::string Description;
        Vector2 Position;
        Vector2 Size{320.0F, 180.0F};
        Color Tint{0.18F, 0.34F, 0.58F, 0.32F};
        float FontSize = 18.0F;
        GraphCommentMoveMode MoveMode = GraphCommentMoveMode::Group;
        AssetId Parent;
        std::vector<AssetId> Members;
        bool Collapsed = false;

        bool operator==(const GraphComment&) const = default;
    };

    struct GraphAuthoringMetadata
    {
        std::vector<GraphNodeAnnotation> NodeAnnotations;
        std::vector<GraphComment> Comments;

        bool operator==(const GraphAuthoringMetadata&) const = default;
    };

    /// Validates bounds, identities, finite geometry, membership, and comment-parent cycles. validNodes contains every
    /// node identity owned by the graph scope; nested comments are resolved from metadata.
    KEIRE_API void ValidateGraphAuthoringMetadata(const GraphAuthoringMetadata& metadata,
                                                  std::span<const AssetId> validNodes);
    /// Removes annotations and direct comment membership for nodes deleted from a graph topology.
    KEIRE_API void RemoveGraphAuthoringNodeReferences(GraphAuthoringMetadata& metadata,
                                                      std::span<const AssetId> removedNodes);
    /// Reassigns a node to the smallest comment rectangle containing its center, or removes direct ownership.
    KEIRE_API void UpdateGraphCommentMembership(GraphAuthoringMetadata& metadata, AssetId node, Vector2 position,
                                                Vector2 size = {220.0F, 120.0F});
    /// Deletes only the comment container. Direct contents are reparented to the deleted comment's parent, if any.
    KEIRE_API void RemoveGraphComment(GraphAuthoringMetadata& metadata, AssetId comment);
} // namespace Keire
