#include "KeireClient/Editor/ShaderGraphDocument.h"

#include "KeireClient/Editor/ShaderGraphBlackboard.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        const Keire::ShaderGraphPin& RequireAnchor(const Keire::ShaderGraphDefinition& definition,
                                                   const Keire::ShaderGraphEndpoint anchor)
        {
            const auto node = std::ranges::find(definition.Nodes, anchor.Node, &Keire::ShaderGraphNode::Id);
            if (node == definition.Nodes.end())
                throw std::invalid_argument("The connection node is no longer available.");
            const auto pin = std::ranges::find(node->Pins, anchor.Pin, &Keire::ShaderGraphPin::Id);
            if (pin == node->Pins.end())
                throw std::invalid_argument("The connection pin is no longer available.");
            return *pin;
        }

        void AppendNode(Keire::ShaderGraphDefinition& definition, const Keire::ShaderGraphNode& node)
        {
            if (node.Kind == Keire::ShaderGraphNodeKind::Keyword &&
                !ShaderGraphHasKeywordToken(definition, node.Symbol))
                definition.Keywords.push_back({.Name = node.Symbol, .DefaultOption = "false"});
            definition.Nodes.push_back(node);
        }
    } // namespace

    bool ShaderGraphNodeSupportsDestination(const Keire::ShaderGraphDefinition& definition,
                                            const Keire::ShaderGraphNode& node,
                                            const std::optional<Keire::ShaderGraphEndpoint> destination)
    {
        const auto* descriptor = Keire::FindShaderGraphNodeDescriptor(
            node.TypeId.empty() ? Keire::ShaderGraphNodeTypeId(node.Kind) : std::string_view(node.TypeId));
        if (!descriptor)
            return false;
        std::uint8_t required = 0;
        std::vector<Keire::ShaderGraphEndpoint> pending;
        if (destination)
            pending.push_back(*destination);
        std::set<Keire::AssetId> visited;
        while (!pending.empty())
        {
            const auto endpoint = pending.back();
            pending.pop_back();
            const auto target = std::ranges::find(definition.Nodes, endpoint.Node, &Keire::ShaderGraphNode::Id);
            if (target == definition.Nodes.end())
                return false;
            const auto pin = std::ranges::find(target->Pins, endpoint.Pin, &Keire::ShaderGraphPin::Id);
            if (pin == target->Pins.end() || pin->Direction != Keire::ShaderGraphPinDirection::Input)
                return false;
            if (target->Kind == Keire::ShaderGraphNodeKind::Master)
            {
                const auto stage = definition.Target.Target == Keire::ShaderGraphTarget::Compute
                                       ? Keire::ShaderGraphShaderStage::Compute
                                   : pin->Name == "WorldPositionOffset" ? Keire::ShaderGraphShaderStage::Vertex
                                                                        : Keire::ShaderGraphShaderStage::Fragment;
                required |= static_cast<std::uint8_t>(stage);
                continue;
            }
            if (!visited.insert(target->Id).second)
                continue;
            for (const auto& connection : definition.Connections)
                if (connection.Output.Node == target->Id)
                    pending.push_back(connection.Input);
        }
        const auto available = static_cast<std::uint8_t>(descriptor->Stages);
        return required != 0 ? (available & required) == required
                             : (available & static_cast<std::uint8_t>(definition.Target.Stages)) != 0;
    }

    bool ShaderGraphDocument::AddConnectedNode(Keire::ShaderGraphNode node, const Keire::ShaderGraphEndpoint anchor)
    {
        const auto& anchorPin = RequireAnchor(Definition(), anchor);
        if (!ShaderGraphNodeSupportsDestination(
                Definition(), node,
                anchorPin.Direction == Keire::ShaderGraphPinDirection::Input ? std::optional(anchor) : std::nullopt))
            throw std::invalid_argument("This node is unavailable in the destination shader stage.");
        for (const auto& pin : node.Pins)
        {
            if (!ShaderGraphPinsCanConnect(anchorPin, pin))
                continue;
            auto candidate = Definition();
            AppendNode(candidate, node);
            const Keire::ShaderGraphEndpoint endpoint{node.Id, pin.Id};
            const bool fromAnchor = anchorPin.Direction == Keire::ShaderGraphPinDirection::Output;
            const auto input = fromAnchor ? endpoint : anchor;
            std::erase_if(candidate.Connections, [&](const auto& cable) { return cable.Input == input; });
            candidate.Connections.push_back({Keire::AssetId::Generate(), fromAnchor ? anchor : endpoint, input});
            try
            {
                Keire::ValidateShaderGraph(candidate);
            }
            catch (const std::invalid_argument&)
            {
                continue;
            }
            return Edit("Create connected Shader Graph node",
                        [&](auto& definition) { definition = std::move(candidate); });
        }
        throw std::invalid_argument("This node has no compatible connection for the selected pin.");
    }

    bool ShaderGraphDocument::InsertNode(Keire::ShaderGraphNode node, const Keire::AssetId connection)
    {
        const auto cable = std::ranges::find(Definition().Connections, connection, &Keire::ShaderGraphConnection::Id);
        if (cable == Definition().Connections.end())
            throw std::invalid_argument("The graph cable is no longer available.");
        if (!ShaderGraphNodeSupportsDestination(Definition(), node, cable->Input))
            throw std::invalid_argument("This node is unavailable in the destination shader stage.");
        const auto& source = RequireAnchor(Definition(), cable->Output);
        const auto& target = RequireAnchor(Definition(), cable->Input);
        for (const auto& input : node.Pins)
        {
            if (input.Direction != Keire::ShaderGraphPinDirection::Input || !ShaderGraphPinsCanConnect(source, input))
                continue;
            for (const auto& output : node.Pins)
            {
                if (output.Direction != Keire::ShaderGraphPinDirection::Output ||
                    !ShaderGraphPinsCanConnect(output, target))
                    continue;
                auto candidate = Definition();
                AppendNode(candidate, node);
                auto replacement =
                    std::ranges::find(candidate.Connections, connection, &Keire::ShaderGraphConnection::Id);
                // Retain the downstream cable identity and route for selections and authored reroutes.
                replacement->Output = {node.Id, output.Id};
                candidate.Connections.push_back({Keire::AssetId::Generate(), cable->Output, {node.Id, input.Id}});
                try
                {
                    Keire::ValidateShaderGraph(candidate);
                }
                catch (const std::invalid_argument&)
                {
                    continue;
                }
                return Edit("Insert Shader Graph node into cable",
                            [&](auto& definition) { definition = std::move(candidate); });
            }
        }
        throw std::invalid_argument("This node cannot connect both ends of the selected cable.");
    }
} // namespace KeireEditor
