#include "KeireClient/Editor/GraphDuplication.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        using IdentityMap = std::map<Keire::AssetId, Keire::AssetId>;

        [[nodiscard]] Keire::AssetId Remapped(const IdentityMap& identities, const Keire::AssetId source)
        {
            const auto found = identities.find(source);
            if (found == identities.end())
                throw std::logic_error("Duplicated graph fragment is missing an identity mapping.");
            return found->second;
        }

        void Offset(Keire::Vector2& position, const Keire::Vector2 offset) noexcept
        {
            position.X += offset.X;
            position.Y += offset.Y;
        }

        void DuplicateAuthoring(Keire::GraphAuthoringMetadata& metadata, IdentityMap& identities,
                                const Keire::Vector2 offset)
        {
            const auto sourceAnnotations = metadata.NodeAnnotations;
            for (const auto& annotation : sourceAnnotations)
            {
                if (const auto node = identities.find(annotation.Node); node != identities.end())
                {
                    auto duplicate = annotation;
                    duplicate.Node = node->second;
                    metadata.NodeAnnotations.push_back(std::move(duplicate));
                }
            }

            const auto sourceComments = metadata.Comments;
            std::set<Keire::AssetId> includedComments;
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (const auto& comment : sourceComments)
                {
                    if (comment.Members.empty() || includedComments.contains(comment.Id))
                        continue;
                    const bool contained = std::ranges::all_of(
                        comment.Members, [&](const Keire::AssetId member)
                        { return identities.contains(member) || includedComments.contains(member); });
                    if (contained)
                        changed = includedComments.insert(comment.Id).second || changed;
                }
            }

            for (const auto& comment : sourceComments)
                if (includedComments.contains(comment.Id))
                    identities.emplace(comment.Id, Keire::AssetId::Generate());
            for (const auto& comment : sourceComments)
            {
                if (!includedComments.contains(comment.Id))
                    continue;
                auto duplicate = comment;
                duplicate.Id = Remapped(identities, comment.Id);
                if (identities.contains(comment.Parent))
                    duplicate.Parent = Remapped(identities, comment.Parent);
                Offset(duplicate.Position, offset);
                for (auto& member : duplicate.Members)
                    member = Remapped(identities, member);
                metadata.Comments.push_back(std::move(duplicate));
            }
        }

        [[nodiscard]] bool Selected(const std::set<Keire::AssetId>& selection, const Keire::AssetId node) noexcept
        {
            return selection.contains(node);
        }

        void RemapShaderNode(Keire::ShaderGraphNode& node, IdentityMap& identities, const Keire::Vector2 offset)
        {
            const auto sourceNode = node.Id;
            node.Id = Keire::AssetId::Generate();
            identities.emplace(sourceNode, node.Id);
            Offset(node.EditorPosition, offset);
            for (auto& pin : node.Pins)
            {
                const auto sourcePin = pin.Id;
                pin.Id = Keire::AssetId::Generate();
                identities.emplace(sourcePin, pin.Id);
            }
        }

        void DuplicateShaderConnections(Keire::ShaderGraphDefinition& definition, const IdentityMap& identities)
        {
            const auto sourceConnections = definition.Connections;
            for (const auto& connection : sourceConnections)
            {
                if (!identities.contains(connection.Output.Node) || !identities.contains(connection.Input.Node))
                    continue;
                auto duplicate = connection;
                duplicate.Id = Keire::AssetId::Generate();
                duplicate.Output.Node = Remapped(identities, connection.Output.Node);
                duplicate.Output.Pin = Remapped(identities, connection.Output.Pin);
                duplicate.Input.Node = Remapped(identities, connection.Input.Node);
                duplicate.Input.Pin = Remapped(identities, connection.Input.Pin);
                definition.Connections.push_back(std::move(duplicate));
            }
        }
    } // namespace

    std::vector<Keire::AssetId> DuplicateShaderGraphSelection(Keire::ShaderGraphDefinition& definition,
                                                              const std::span<const Keire::AssetId> selection,
                                                              const Keire::Vector2 offset)
    {
        const std::set selected(selection.begin(), selection.end());
        IdentityMap identities;
        std::vector<Keire::AssetId> result;
        const auto sourceNodes = definition.Nodes;
        for (auto node : sourceNodes)
        {
            if (!Selected(selected, node.Id) || node.Kind == Keire::ShaderGraphNodeKind::Master)
                continue;
            RemapShaderNode(node, identities, offset);
            result.push_back(node.Id);
            definition.Nodes.push_back(std::move(node));
        }
        DuplicateShaderConnections(definition, identities);
        DuplicateAuthoring(definition.Authoring, identities, offset);
        return result;
    }

    std::vector<Keire::AssetId> DuplicateMaterialGraphSelection(Keire::MaterialGraphDefinition& definition,
                                                                const std::span<const Keire::AssetId> selection,
                                                                const Keire::Vector2 offset)
    {
        const std::set selected(selection.begin(), selection.end());
        IdentityMap identities;
        std::vector<Keire::AssetId> result;
        const auto sourceNodes = definition.Nodes;
        for (auto node : sourceNodes)
        {
            if (!Selected(selected, node.Id))
                continue;
            const auto sourceNode = node.Id;
            const auto sourcePin = node.OutputPin;
            node.Id = Keire::AssetId::Generate();
            node.OutputPin = Keire::AssetId::Generate();
            identities.emplace(sourceNode, node.Id);
            identities.emplace(sourcePin, node.OutputPin);
            Offset(node.EditorPosition, offset);
            result.push_back(node.Id);
            definition.Nodes.push_back(std::move(node));
        }

        const auto sourceExpressions = definition.SurfaceGraph.Nodes;
        for (auto node : sourceExpressions)
        {
            if (!Selected(selected, node.Id) || node.Kind == Keire::ShaderGraphNodeKind::Master)
                continue;
            RemapShaderNode(node, identities, offset);
            result.push_back(node.Id);
            definition.SurfaceGraph.Nodes.push_back(std::move(node));
        }

        const auto sourceConnections = definition.Connections;
        for (const auto& connection : sourceConnections)
        {
            if (!identities.contains(connection.Output.Node) || !identities.contains(connection.Input.Node))
                continue;
            auto duplicate = connection;
            duplicate.Id = Keire::AssetId::Generate();
            duplicate.Output = {Remapped(identities, connection.Output.Node),
                                Remapped(identities, connection.Output.Pin)};
            duplicate.Input = {Remapped(identities, connection.Input.Node), Remapped(identities, connection.Input.Pin)};
            definition.Connections.push_back(std::move(duplicate));
        }
        DuplicateShaderConnections(definition.SurfaceGraph, identities);
        DuplicateAuthoring(definition.Authoring, identities, offset);
        DuplicateAuthoring(definition.SurfaceGraph.Authoring, identities, offset);
        return result;
    }

    std::vector<Keire::AssetId> DuplicateVfxGraphSelection(Keire::VfxEffectDefinition& definition,
                                                           const Keire::AssetId system,
                                                           const std::span<const Keire::AssetId> selection,
                                                           const Keire::Vector2 offset)
    {
        auto foundSystem = std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
        if (foundSystem == definition.Systems.end())
            throw std::invalid_argument("VFX graph system does not exist.");
        const std::set selected(selection.begin(), selection.end());
        IdentityMap identities;
        std::vector<Keire::AssetId> result;
        const auto sourceNodes = foundSystem->Nodes;
        for (auto node : sourceNodes)
        {
            if (!Selected(selected, node.Id) || node.Kind == Keire::VfxGraphNodeKind::Context)
                continue;
            const auto sourceNode = node.Id;
            node.Id = Keire::AssetId::Generate();
            identities.emplace(sourceNode, node.Id);
            Offset(node.EditorPosition, offset);
            for (auto& pin : node.Pins)
            {
                const auto sourcePin = pin.Id;
                pin.Id = Keire::AssetId::Generate();
                identities.emplace(sourcePin, pin.Id);
            }
            for (auto& pin : node.DynamicPinOrder)
                pin = Remapped(identities, pin);
            for (auto& block : node.Blocks)
            {
                const auto sourceBlock = block.Id;
                block.Id = Keire::AssetId::Generate();
                identities.emplace(sourceBlock, block.Id);
                for (auto& pin : block.Pins)
                {
                    const auto sourcePin = pin.Id;
                    pin.Id = Keire::AssetId::Generate();
                    identities.emplace(sourcePin, pin.Id);
                }
            }
            result.push_back(node.Id);
            foundSystem->Nodes.push_back(std::move(node));
        }

        const auto sourceConnections = foundSystem->Connections;
        for (const auto& connection : sourceConnections)
        {
            if (!identities.contains(connection.OutputNode) || !identities.contains(connection.InputNode))
                continue;
            auto duplicate = connection;
            duplicate.Id = Keire::AssetId::Generate();
            duplicate.OutputNode = Remapped(identities, connection.OutputNode);
            duplicate.OutputPin = Remapped(identities, connection.OutputPin);
            duplicate.InputNode = Remapped(identities, connection.InputNode);
            duplicate.InputPin = Remapped(identities, connection.InputPin);
            if (connection.OutputBlock)
                duplicate.OutputBlock = Remapped(identities, connection.OutputBlock);
            if (connection.InputBlock)
                duplicate.InputBlock = Remapped(identities, connection.InputBlock);
            foundSystem->Connections.push_back(std::move(duplicate));
        }
        DuplicateAuthoring(foundSystem->Authoring, identities, offset);
        return result;
    }
} // namespace KeireEditor
