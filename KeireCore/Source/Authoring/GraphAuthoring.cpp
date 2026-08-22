#include "Keire/Authoring/GraphAuthoring.h"
#include "KeireInternal/Authoring/GraphAuthoringSerialization.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        constexpr std::size_t MaximumAnnotations = 1024;
        constexpr std::size_t MaximumComments = 256;
        constexpr std::size_t MaximumCommentMembers = 1024;
        constexpr std::size_t MaximumTitleBytes = 256;
        constexpr std::size_t MaximumDescriptionBytes = 4096;
        constexpr std::size_t MaximumAnnotationBytes = 4096;
        constexpr std::size_t MaximumCommentDepth = 16;

        [[nodiscard]] bool ValidSize(const Vector2 size) noexcept
        {
            return Math::IsFinite(size) && size.X >= 48.0F && size.Y >= 32.0F && size.X <= 100000.0F &&
                   size.Y <= 100000.0F;
        }
    } // namespace

    void ValidateGraphAuthoringMetadata(const GraphAuthoringMetadata& metadata,
                                        const std::span<const AssetId> validNodes)
    {
        if (metadata.NodeAnnotations.size() > MaximumAnnotations || metadata.Comments.size() > MaximumComments)
            throw std::invalid_argument("Graph authoring metadata exceeds its collection bounds.");

        const std::set<AssetId> nodes(validNodes.begin(), validNodes.end());
        std::set<AssetId> annotated;
        for (const auto& annotation : metadata.NodeAnnotations)
        {
            if (!annotation.Node || !nodes.contains(annotation.Node) ||
                annotation.Text.size() > MaximumAnnotationBytes || !annotated.insert(annotation.Node).second)
                throw std::invalid_argument("Graph node annotation is invalid.");
        }

        std::map<AssetId, const GraphComment*> comments;
        for (const auto& comment : metadata.Comments)
        {
            if (!comment.Id || nodes.contains(comment.Id) || comment.Title.empty() ||
                comment.Title.size() > MaximumTitleBytes || comment.Description.size() > MaximumDescriptionBytes ||
                !Math::IsFinite(comment.Position) || !ValidSize(comment.Size) || !Math::IsFinite(comment.Tint) ||
                comment.Tint.Red < 0.0F || comment.Tint.Red > 1.0F || comment.Tint.Green < 0.0F ||
                comment.Tint.Green > 1.0F || comment.Tint.Blue < 0.0F || comment.Tint.Blue > 1.0F ||
                comment.Tint.Alpha < 0.0F || comment.Tint.Alpha > 1.0F || !std::isfinite(comment.FontSize) ||
                comment.FontSize < 8.0F || comment.FontSize > 96.0F ||
                comment.MoveMode > GraphCommentMoveMode::CommentOnly ||
                comment.Members.size() > MaximumCommentMembers || !comments.emplace(comment.Id, &comment).second)
                throw std::invalid_argument("Graph comment is invalid.");
        }

        std::map<AssetId, AssetId> directOwners;
        for (const auto& [id, comment] : comments)
        {
            if (comment->Parent && (!comments.contains(comment->Parent) || comment->Parent == id))
                throw std::invalid_argument("Graph comment parent is invalid.");
            std::set<AssetId> members;
            for (const auto member : comment->Members)
                if (!member || member == id || (!nodes.contains(member) && !comments.contains(member)) ||
                    !members.insert(member).second || !directOwners.emplace(member, id).second)
                    throw std::invalid_argument("Graph comment member is invalid.");

            std::set<AssetId> ancestors{id};
            auto parent = comment->Parent;
            for (std::size_t depth = 0; parent; ++depth)
            {
                if (depth >= MaximumCommentDepth || !ancestors.insert(parent).second)
                    throw std::invalid_argument("Graph comment ownership contains a cycle or exceeds its depth bound.");
                parent = comments.at(parent)->Parent;
            }
        }

        for (const auto& [id, comment] : comments)
        {
            const auto owner = directOwners.find(id);
            if ((comment->Parent && (owner == directOwners.end() || owner->second != comment->Parent)) ||
                (!comment->Parent && owner != directOwners.end()))
                throw std::invalid_argument("Graph comment parent and membership disagree.");
        }
    }

    void RemoveGraphAuthoringNodeReferences(GraphAuthoringMetadata& metadata,
                                            const std::span<const AssetId> removedNodes)
    {
        const auto removed = [&](const AssetId candidate)
        { return std::ranges::find(removedNodes, candidate) != removedNodes.end(); };
        std::erase_if(metadata.NodeAnnotations,
                      [&](const GraphNodeAnnotation& annotation) { return removed(annotation.Node); });
        for (auto& comment : metadata.Comments)
            std::erase_if(comment.Members, removed);
    }

    void UpdateGraphCommentMembership(GraphAuthoringMetadata& metadata, const AssetId node, const Vector2 position,
                                      const Vector2 size)
    {
        if (!node || !Math::IsFinite(position) || !Math::IsFinite(size) || size.X < 0.0F || size.Y < 0.0F)
            throw std::invalid_argument("Graph comment membership geometry is invalid.");

        for (auto& comment : metadata.Comments)
            std::erase(comment.Members, node);
        const Vector2 center{position.X + size.X * 0.5F, position.Y + size.Y * 0.5F};
        GraphComment* owner = nullptr;
        float ownerArea = std::numeric_limits<float>::max();
        for (auto& comment : metadata.Comments)
        {
            const bool contains = center.X >= comment.Position.X && center.Y >= comment.Position.Y &&
                                  center.X <= comment.Position.X + comment.Size.X &&
                                  center.Y <= comment.Position.Y + comment.Size.Y;
            const float area = comment.Size.X * comment.Size.Y;
            if (contains && area < ownerArea)
            {
                owner = &comment;
                ownerArea = area;
            }
        }
        if (owner)
            owner->Members.push_back(node);
    }

    void RemoveGraphComment(GraphAuthoringMetadata& metadata, const AssetId comment)
    {
        const auto found = std::ranges::find(metadata.Comments, comment, &GraphComment::Id);
        if (found == metadata.Comments.end())
            throw std::invalid_argument("Graph comment is unavailable.");
        const auto parentId = found->Parent;
        const auto members = found->Members;
        if (parentId)
        {
            auto parent = std::ranges::find(metadata.Comments, parentId, &GraphComment::Id);
            if (parent == metadata.Comments.end())
                throw std::invalid_argument("Graph comment parent is unavailable.");
            std::erase(parent->Members, comment);
            parent->Members.insert(parent->Members.end(), members.begin(), members.end());
        }
        for (auto& candidate : metadata.Comments)
            if (candidate.Parent == comment)
                candidate.Parent = parentId;
        metadata.Comments.erase(found);
    }

    namespace Detail
    {
        nlohmann::json EncodeGraphAuthoringMetadata(const GraphAuthoringMetadata& metadata)
        {
            nlohmann::json annotations = nlohmann::json::array();
            for (const auto& annotation : metadata.NodeAnnotations)
                annotations.push_back({{"node", annotation.Node.ToString()},
                                       {"text", annotation.Text},
                                       {"visible", annotation.Visible},
                                       {"pinned", annotation.Pinned}});

            nlohmann::json comments = nlohmann::json::array();
            for (const auto& comment : metadata.Comments)
            {
                nlohmann::json members = nlohmann::json::array();
                for (const auto member : comment.Members)
                    members.push_back(member.ToString());
                comments.push_back(
                    {{"id", comment.Id.ToString()},
                     {"title", comment.Title},
                     {"description", comment.Description},
                     {"position", {comment.Position.X, comment.Position.Y}},
                     {"size", {comment.Size.X, comment.Size.Y}},
                     {"color", {comment.Tint.Red, comment.Tint.Green, comment.Tint.Blue, comment.Tint.Alpha}},
                     {"fontSize", comment.FontSize},
                     {"moveMode", static_cast<std::uint8_t>(comment.MoveMode)},
                     {"parent", comment.Parent ? nlohmann::json(comment.Parent.ToString()) : nlohmann::json(nullptr)},
                     {"members", std::move(members)},
                     {"collapsed", comment.Collapsed}});
            }
            return {{"annotations", std::move(annotations)}, {"comments", std::move(comments)}};
        }

        GraphAuthoringMetadata DecodeGraphAuthoringMetadata(const nlohmann::json& source,
                                                            const std::span<const AssetId> validNodes)
        {
            if (!source.is_object())
                throw std::invalid_argument("Graph authoring metadata must be an object.");
            GraphAuthoringMetadata result;
            const auto& annotations = source.value("annotations", nlohmann::json::array());
            const auto& comments = source.value("comments", nlohmann::json::array());
            if (!annotations.is_array() || !comments.is_array() || annotations.size() > MaximumAnnotations ||
                comments.size() > MaximumComments)
                throw std::invalid_argument("Graph authoring metadata collections exceed their bounds.");

            for (const auto& encoded : annotations)
                result.NodeAnnotations.push_back({AssetId::Parse(encoded.at("node").get<std::string>()),
                                                  encoded.value("text", std::string{}), encoded.value("visible", true),
                                                  encoded.value("pinned", false)});
            for (const auto& encoded : comments)
            {
                GraphComment comment;
                comment.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                comment.Title = encoded.value("title", std::string("Comment"));
                comment.Description = encoded.value("description", std::string{});
                const auto& position = encoded.at("position");
                const auto& size = encoded.at("size");
                const auto& color = encoded.at("color");
                if (!position.is_array() || position.size() != 2 || !size.is_array() || size.size() != 2 ||
                    !color.is_array() || color.size() != 4)
                    throw std::invalid_argument("Graph comment geometry is invalid.");
                comment.Position = {position.at(0).get<float>(), position.at(1).get<float>()};
                comment.Size = {size.at(0).get<float>(), size.at(1).get<float>()};
                comment.Tint = {color.at(0).get<float>(), color.at(1).get<float>(), color.at(2).get<float>(),
                                color.at(3).get<float>()};
                comment.FontSize = encoded.value("fontSize", 18.0F);
                comment.MoveMode = static_cast<GraphCommentMoveMode>(encoded.value("moveMode", std::uint8_t{}));
                if (encoded.contains("parent") && !encoded.at("parent").is_null())
                    comment.Parent = AssetId::Parse(encoded.at("parent").get<std::string>());
                const auto& members = encoded.value("members", nlohmann::json::array());
                if (!members.is_array() || members.size() > MaximumCommentMembers)
                    throw std::invalid_argument("Graph comment members exceed their bounds.");
                for (const auto& member : members)
                    comment.Members.push_back(AssetId::Parse(member.get<std::string>()));
                comment.Collapsed = encoded.value("collapsed", false);
                result.Comments.push_back(std::move(comment));
            }
            ValidateGraphAuthoringMetadata(result, validNodes);
            return result;
        }
    } // namespace Detail
} // namespace Keire
