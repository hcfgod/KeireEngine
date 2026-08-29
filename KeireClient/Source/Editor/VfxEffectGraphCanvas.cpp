#include "KeireClientInternal/Editor/VfxEffectPanelInternal.h"

#include <algorithm>

namespace KeireEditor
{
    void VfxEffectPanel::DrawGraphCanvas(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.VfxEffectTheme();
        const auto system = std::ranges::find(definition.Systems, m_SelectedSystem, &Keire::VfxGraphSystem::Id);
        if (system == definition.Systems.end())
        {
            ui.TextColored(theme.MutedText, "Create or select a system to begin authoring.");
            return;
        }
        const auto graphViewportSize = ui.ContentAvailable();
        ui.TextColored(theme.Accent, system->Name);
        ui.SameLine();
        bool nodePaletteMenuOpen = false;
        if (auto disabled = ui.BeginDisabled(definition.ExecutionSource != Keire::VfxExecutionSource::Graph); disabled)
        {
            if (auto add = ui.BeginCombo("Add Node", "Choose..."); add)
            {
                nodePaletteMenuOpen = true;
                if (!m_NodePaletteMenuOpen)
                {
                    m_NodePaletteSearch.clear();
                    m_NodeMenuSelection.Open();
                }
                if (m_NodeMenuSelection.ConsumeFocusRequest())
                    ui.RequestKeyboardFocus();
                (void)ui.InputTextWithHint("##VfxToolbarNodeSearch", "Search nodes...", m_NodePaletteSearch);
                ui.Separator();
                const auto zoom = m_GraphCanvas.Zoom();
                const Keire::Vector2 position{graphViewportSize.Width * 0.5F / zoom - m_GraphCanvas.Pan().X - 130.0F,
                                              graphViewportSize.Height * 0.4F / zoom - m_GraphCanvas.Pan().Y - 48.0F};
                if (DrawNodePaletteEntries(ui, system->Id, position, m_NodePaletteSearch))
                    return;
            }
        }
        m_NodePaletteMenuOpen = nodePaletteMenuOpen;
        if (definition.ExecutionSource != Keire::VfxExecutionSource::Graph)
        {
            ui.SameLine();
            ui.TextColored(theme.Warning, "Convert to Graph to add executable nodes.");
        }
        std::vector<NodeGraphNode> nodes;
        nodes.reserve(system->Nodes.size());
        std::vector<std::pair<StableNodeId, Keire::AssetId>> nodeIdentities;
        StableNodeGraphIdMap nodeIds;
        StableNodeGraphIdMap blockIds;
        StableNodeGraphIdMap pinIds;
        for (const auto& node : system->Nodes)
        {
            const auto inputs =
                static_cast<std::size_t>(std::ranges::count(node.Pins, true, &Keire::VfxGraphPin::Input));
            const auto outputs = node.Pins.size() - inputs;
            const auto rows = std::max(inputs, outputs);
            NodeGraphNode canvasNode{
                .Id = nodeIds.Assign(node.Id, PreferredCanvasId(node.Id, 0x5646584e4f444501ULL)),
                .Label = NodeLabel(definition, node),
                .Position = node.EditorPosition,
                .Size = {260.0F, std::max(98.0F, 70.0F + static_cast<float>(rows) * 24.0F)},
                .Color = NodeColor(node),
                .Subtitle = std::string(VfxGraphNodeKindLabel(node.Kind)) + "  |  " +
                            std::string(EnumName(node.Context, ContextTypes)),
            };
            nodeIdentities.emplace_back(canvasNode.Id, node.Id);
            canvasNode.Pins.reserve(node.Pins.size());
            for (const auto& pin : node.Pins)
            {
                canvasNode.Pins.push_back(
                    {.Id = pinIds.Assign(pin.Id, PreferredCanvasId(pin.Id, 0x56465850494e0001ULL)),
                     .Label = pin.Name + "  [" + std::string(EnumName(pin.Type, GraphValueTypes)) + "]",
                     .Direction = pin.Input ? NodeGraphPinDirection::Input : NodeGraphPinDirection::Output,
                     .Type = static_cast<StableNodeId>(pin.Type) + 1,
                     .Color = PinColor(pin.Type)});
            }
            canvasNode.Blocks.reserve(node.Blocks.size());
            canvasNode.Deletable = node.Kind != Keire::VfxGraphNodeKind::Context;
            for (const auto& block : node.Blocks)
            {
                NodeGraphBlockRow row{
                    .Id = blockIds.Assign(block.Id, PreferredCanvasId(block.Id, 0x564658424c4f434bULL)),
                    .Label = block.Type,
                    .Enabled = block.Enabled,
                    .Color = {0.12F, 0.16F, 0.22F, 1.0F},
                };
                row.Pins.reserve(block.Pins.size());
                for (const auto& pin : block.Pins)
                {
                    row.Pins.push_back(
                        {.Id = pinIds.Assign(pin.Id, PreferredCanvasId(pin.Id, 0x5646584250494e01ULL)),
                         .Label = pin.Name + "  [" + std::string(EnumName(pin.Type, GraphValueTypes)) + "]",
                         .Direction = pin.Input ? NodeGraphPinDirection::Input : NodeGraphPinDirection::Output,
                         .Type = static_cast<StableNodeId>(pin.Type) + 1,
                         .Color = PinColor(pin.Type)});
                }
                canvasNode.Blocks.push_back(std::move(row));
            }
            nodes.push_back(std::move(canvasNode));
        }
        std::vector<NodeGraphConnection> connections;
        connections.reserve(system->Connections.size());
        ApplyNodeGraphAnnotations(system->Authoring, nodeIdentities, nodes);
        auto comments = BuildNodeGraphCommentModel(system->Authoring, nodeIdentities);
        StableNodeGraphIdMap connectionIds;
        std::vector<std::pair<StableNodeId, Keire::AssetId>> connectionIdentities;
        const auto findEndpointPin = [&](const Keire::VfxGraphEndpoint endpoint) -> const Keire::VfxGraphPin*
        {
            const auto node = std::ranges::find(system->Nodes, endpoint.Node, &Keire::VfxGraphNode::Id);
            if (node == system->Nodes.end())
                return nullptr;
            if (endpoint.Block)
            {
                const auto block = std::ranges::find(node->Blocks, endpoint.Block, &Keire::VfxGraphBlock::Id);
                if (block == node->Blocks.end())
                    return nullptr;
                const auto pin = std::ranges::find(block->Pins, endpoint.Pin, &Keire::VfxGraphPin::Id);
                return pin == block->Pins.end() ? nullptr : std::addressof(*pin);
            }
            const auto pin = std::ranges::find(node->Pins, endpoint.Pin, &Keire::VfxGraphPin::Id);
            return pin == node->Pins.end() ? nullptr : std::addressof(*pin);
        };
        for (const auto& connection : system->Connections)
        {
            const auto source = nodeIds.Find(connection.OutputNode);
            const auto target = nodeIds.Find(connection.InputNode);
            const auto sourceBlock =
                connection.OutputBlock ? blockIds.Find(connection.OutputBlock) : std::optional<StableNodeId>{0};
            const auto targetBlock =
                connection.InputBlock ? blockIds.Find(connection.InputBlock) : std::optional<StableNodeId>{0};
            const auto sourcePin = pinIds.Find(connection.OutputPin);
            const auto targetPin = pinIds.Find(connection.InputPin);
            if (!source || !target || !sourceBlock || !targetBlock || !sourcePin || !targetPin)
                continue;
            std::string label;
            const auto* graphPin = findEndpointPin(connection.OutputEndpoint());
            if (graphPin && graphPin->Type != Keire::VfxValueType::ParticleStream)
                label = std::string(EnumName(graphPin->Type, GraphValueTypes));
            connections.push_back({
                .Id = connectionIds.Assign(connection.Id, PreferredCanvasId(connection.Id, 0x5646584c494e4b01ULL)),
                .Source = *source,
                .Target = *target,
                .Label = std::move(label),
                .SourcePin = *sourcePin,
                .TargetPin = *targetPin,
                .SourceBlock = *sourceBlock,
                .TargetBlock = *targetBlock,
                .RoutingPoints = connection.RoutingPoints,
            });
            connectionIdentities.emplace_back(connections.back().Id, connection.Id);
        }
        if (m_SelectedNode && !nodeIds.Find(m_SelectedNode))
        {
            m_SelectedNode = {};
            m_SelectedBlock = {};
        }
        std::optional<NodeGraphBlockAddress> selectedBlock;
        if (m_SelectedBlock)
        {
            const auto owner =
                std::ranges::find_if(system->Nodes,
                                     [&](const Keire::VfxGraphNode& node)
                                     {
                                         return std::ranges::find(node.Blocks, m_SelectedBlock,
                                                                  &Keire::VfxGraphBlock::Id) != node.Blocks.end();
                                     });
            const auto canvasNode = owner == system->Nodes.end() ? std::nullopt : nodeIds.Find(owner->Id);
            const auto canvasBlock = blockIds.Find(m_SelectedBlock);
            if (!canvasNode || !canvasBlock)
                m_SelectedBlock = {};
            else
            {
                m_SelectedNode = owner->Id;
                selectedBlock = NodeGraphBlockAddress{*canvasNode, *canvasBlock};
            }
        }
        if (m_SelectedConnection && !connectionIds.Find(m_SelectedConnection))
            m_SelectedConnection = {};
        SynchronizeGraphSelection(m_GraphCanvas, nodeIdentities, m_SelectedNodes,
                                  m_SelectedNode ? std::optional(m_SelectedNode) : std::nullopt);
        m_GraphCanvas.SelectBlock(selectedBlock);
        m_GraphCanvas.SelectConnection(connectionIds.Find(m_SelectedConnection));
        if (ui.Button("Frame All"))
            m_GraphCanvas.Focus(nodes, ui.ContentAvailable());
        ui.SameLine();
        if (DrawGraphArrangeMenu(ui, nodes, connections, nodeIdentities, connectionIdentities))
            return;
        ui.SameLine();
        (void)DrawGraphBookmarkMenu(ui, m_GraphBookmarks, m_GraphCanvas);
        ui.SameLine();
        if (m_GraphCanvas.ConnectionDragActive())
            ui.TextColored(theme.Warning, "Release over a compatible pin  |  Escape cancels");
        else
            ui.TextColored(theme.MutedText,
                           "Right-click to add  |  drag pins to connect  |  double-click cables for routing points");
        const auto findGraphNode = [&](const StableNodeId canvasId) -> const Keire::VfxGraphNode*
        {
            const auto found = std::ranges::find_if(system->Nodes, [&](const Keire::VfxGraphNode& node)
                                                    { return nodeIds.Find(node.Id) == canvasId; });
            return found == system->Nodes.end() ? nullptr : std::addressof(*found);
        };
        const auto findGraphBlock = [&](const NodeGraphBlockAddress address)
            -> std::optional<std::pair<const Keire::VfxGraphNode*, const Keire::VfxGraphBlock*>>
        {
            const auto* node = findGraphNode(address.Node);
            if (!node)
                return std::nullopt;
            const auto block = std::ranges::find_if(node->Blocks, [&](const Keire::VfxGraphBlock& candidate)
                                                    { return blockIds.Find(candidate.Id) == address.Block; });
            if (block == node->Blocks.end())
                return std::nullopt;
            return std::pair{node, std::addressof(*block)};
        };
        const auto findGraphPin = [&](const StableNodeId canvasNode, const StableNodeId canvasBlock,
                                      const StableNodeId canvasPin) -> std::optional<Keire::VfxGraphEndpoint>
        {
            const auto* node = findGraphNode(canvasNode);
            if (!node)
                return std::nullopt;
            if (canvasBlock)
            {
                const auto block = std::ranges::find_if(node->Blocks, [&](const Keire::VfxGraphBlock& candidate)
                                                        { return blockIds.Find(candidate.Id) == canvasBlock; });
                if (block == node->Blocks.end())
                    return std::nullopt;
                const auto pin = std::ranges::find_if(block->Pins, [&](const Keire::VfxGraphPin& candidate)
                                                      { return pinIds.Find(candidate.Id) == canvasPin; });
                if (pin == block->Pins.end())
                    return std::nullopt;
                return Keire::VfxGraphEndpoint{node->Id, block->Id, pin->Id};
            }
            const auto pin = std::ranges::find_if(node->Pins, [&](const Keire::VfxGraphPin& candidate)
                                                  { return pinIds.Find(candidate.Id) == canvasPin; });
            if (pin == node->Pins.end())
                return std::nullopt;
            return Keire::VfxGraphEndpoint{node->Id, {}, pin->Id};
        };
        const auto findGraphConnection = [&](const StableNodeId canvasId) -> const Keire::VfxGraphConnection*
        {
            const auto found =
                std::ranges::find_if(system->Connections, [&](const Keire::VfxGraphConnection& connection)
                                     { return connectionIds.Find(connection.Id) == canvasId; });
            return found == system->Connections.end() ? nullptr : std::addressof(*found);
        };
        NodeGraphCanvasOptions options{
            .Editable = true,
            .ValidateConnection =
                [&](const NodeGraphConnectionRequest& request)
            {
                const auto source = findGraphPin(request.SourceNode, request.SourceBlock, request.SourcePin);
                const auto target = findGraphPin(request.TargetNode, request.TargetBlock, request.TargetPin);
                if (!source || !target)
                {
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                         "A connection endpoint is unavailable."};
                }
                const auto check = document.CheckConnection(system->Id, *source, *target);
                switch (check.Status)
                {
                case VfxGraphConnectionStatus::Accepted:
                    if (check.ReplacesInput)
                    {
                        return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::AcceptWithWarning,
                                                             "Replaces the cable currently driving this input."};
                    }
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Accept, {}};
                case VfxGraphConnectionStatus::AcceptedWithWarning:
                {
                    auto diagnostic = std::string("Connection is valid; the graph remains incomplete.");
                    if (!check.Diagnostic.empty())
                        diagnostic += " " + check.Diagnostic;
                    if (check.ReplacesInput)
                        diagnostic += " The existing input cable will be replaced.";
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::AcceptWithWarning,
                                                         std::move(diagnostic)};
                }
                case VfxGraphConnectionStatus::Rejected:
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject, check.Diagnostic};
                }
                return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                     "The connection could not be validated."};
            },
            .EditableReroutes = true,
            .MultiSelection = true,
            .Comments = comments.Comments,
        };
        const auto result = m_GraphCanvas.Draw(ui, "VfxNodeCanvas", nodes, connections, options);
        if (DrawGraphComments(ui, system->Id, nodeIdentities, nodes, comments, result))
            return;
        m_SelectedNodes = ResolveGraphSelection(result.SelectedNodes, nodeIdentities);
        m_SelectedNode = m_SelectedNodes.empty() ? Keire::AssetId{} : m_SelectedNodes.back();
        if (HandleGraphClipboard(result, nodeIdentities))
            return;
        if (!result.DuplicateNodesRequested.empty())
            return DuplicateGraphSelection(result.DuplicateNodesRequested, nodeIdentities);
        const auto renderedCanvasSize = ui.LastItemRect().Size();
        const auto setRouting = [&](const StableNodeId canvasConnection, std::vector<Keire::Vector2> routing) -> bool
        {
            const auto* connection = findGraphConnection(canvasConnection);
            if (!connection)
                return false;
            return ApplyAction("Routed VFX graph cable", [&document, graph = system->Id, connection = connection->Id,
                                                          routing = std::move(routing)]() mutable
                               { return document.SetConnectionRouting(graph, connection, std::move(routing)); });
        };
        if (result.AddRerouteRequested)
        {
            const auto connection =
                std::ranges::find(connections, result.AddRerouteRequested->Connection, &NodeGraphConnection::Id);
            if (connection != connections.end() &&
                result.AddRerouteRequested->Index <= connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing.insert(routing.begin() + static_cast<std::ptrdiff_t>(result.AddRerouteRequested->Index),
                               result.AddRerouteRequested->GraphPosition);
                if (setRouting(connection->Id, std::move(routing)))
                    return;
            }
        }
        if (result.MoveRerouteRequested)
        {
            const auto connection =
                std::ranges::find(connections, result.MoveRerouteRequested->Connection, &NodeGraphConnection::Id);
            if (connection != connections.end() &&
                result.MoveRerouteRequested->Index < connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing[result.MoveRerouteRequested->Index] = result.MoveRerouteRequested->GraphPosition;
                if (setRouting(connection->Id, std::move(routing)))
                    return;
            }
        }
        if (result.DeleteRerouteRequested)
        {
            const auto connection =
                std::ranges::find(connections, result.DeleteRerouteRequested->Connection, &NodeGraphConnection::Id);
            if (connection != connections.end() &&
                result.DeleteRerouteRequested->Index < connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing.erase(routing.begin() + static_cast<std::ptrdiff_t>(result.DeleteRerouteRequested->Index));
                if (setRouting(connection->Id, std::move(routing)))
                    return;
            }
        }
        if (result.DeleteConnectionRequested)
        {
            if (const auto* connection = findGraphConnection(*result.DeleteConnectionRequested);
                connection &&
                ApplyAction("Unlinked VFX graph cable", [&document, graph = system->Id, cable = connection->Id]
                            { return document.RemoveConnection(graph, cable); }))
            {
                m_SelectedConnection = {};
                m_GraphCanvas.SelectConnection(std::nullopt);
                return;
            }
        }
        if (result.DeleteBlockRequested)
        {
            if (const auto block = findGraphBlock(*result.DeleteBlockRequested);
                block &&
                ApplyAction("Removed VFX Context Block",
                            [&document, graph = system->Id, context = block->first->Id, blockId = block->second->Id]
                            { return document.RemoveBlock(graph, context, blockId); }))
            {
                m_SelectedBlock = {};
                m_SelectedConnection = {};
                m_GraphCanvas.SelectBlock(std::nullopt);
                m_GraphCanvas.SelectConnection(std::nullopt);
                return;
            }
        }
        if (!result.DeleteNodesRequested.empty())
        {
            std::vector<Keire::AssetId> selected;
            for (const auto canvasNode : result.DeleteNodesRequested)
                if (const auto* node = findGraphNode(canvasNode))
                    selected.push_back(node->Id);
            if (ApplyAction(selected.size() == 1 ? "Removed VFX graph node" : "Removed VFX graph nodes",
                            [&document, graph = system->Id, selected]
                            { return document.RemoveNodes(graph, selected); }))
            {
                m_SelectedNode = {};
                m_SelectedNodes.clear();
                m_SelectedBlock = {};
                m_SelectedConnection = {};
                m_GraphCanvas.Select(std::nullopt);
                m_GraphCanvas.SelectBlock(std::nullopt);
                m_GraphCanvas.SelectConnection(std::nullopt);
                return;
            }
        }
        if (!result.ProtectedNodes.empty())
            m_Message = "Executable VFX Context nodes are protected and were not deleted.";
        if (result.ConnectionRequested)
        {
            const auto source =
                findGraphPin(result.ConnectionRequested->SourceNode, result.ConnectionRequested->SourceBlock,
                             result.ConnectionRequested->SourcePin);
            const auto target =
                findGraphPin(result.ConnectionRequested->TargetNode, result.ConnectionRequested->TargetBlock,
                             result.ConnectionRequested->TargetPin);
            if (source && target)
            {
                const auto check = document.CheckConnection(system->Id, *source, *target);
                if (check.Status != VfxGraphConnectionStatus::Rejected)
                {
                    Keire::VfxGraphConnection connection;
                    connection.Id = Keire::AssetId::Generate();
                    connection.OutputNode = source->Node;
                    connection.OutputPin = source->Pin;
                    connection.InputNode = target->Node;
                    connection.InputPin = target->Pin;
                    connection.OutputBlock = source->Block;
                    connection.InputBlock = target->Block;
                    const auto connectionId = connection.Id;
                    if (ApplyAction(check.ReplacesInput ? "Rewired VFX graph input" : "Connected VFX graph pins",
                                    [&document, graph = system->Id, connection = connection]() mutable
                                    { return document.AddConnection(graph, connection); }))
                    {
                        m_SelectedNode = {};
                        m_SelectedBlock = {};
                        m_SelectedConnection = connectionId;
                        m_GraphCanvas.Select(std::nullopt);
                        m_GraphCanvas.SelectBlock(std::nullopt);
                        return;
                    }
                }
                else
                {
                    m_Message = check.Diagnostic;
                }
            }
        }
        if (result.ActivatedNode)
        {
            if (const auto* node = findGraphNode(*result.ActivatedNode); node)
            {
                m_SelectedNode = node->Id;
                if (!result.ActivatedBlock)
                    m_SelectedBlock = {};
                m_SelectedConnection = {};
            }
        }
        if (result.ActivatedBlock)
        {
            if (const auto block = findGraphBlock(*result.ActivatedBlock); block)
            {
                m_SelectedNode = block->first->Id;
                m_SelectedBlock = block->second->Id;
                m_SelectedConnection = {};
            }
        }
        if (result.ActivatedConnection)
        {
            if (const auto* connection = findGraphConnection(*result.ActivatedConnection); connection)
            {
                m_SelectedConnection = connection->Id;
                m_SelectedNode = {};
                m_SelectedBlock = {};
            }
        }
        if (result.BackgroundActivated)
        {
            m_SelectedNode = {};
            m_SelectedBlock = {};
            m_SelectedConnection = {};
        }
        if (result.ContextRequested)
        {
            m_ContextNode = {};
            m_ContextBlock = {};
            m_ContextPin = {};
            m_ContextConnection = {};
            m_NodePalettePosition = result.ContextRequested->GraphPosition;
            switch (result.ContextRequested->Kind)
            {
            case NodeGraphContextTargetKind::Background:
                m_NodePaletteSearch.clear();
                m_NodeMenuSelection.Open();
                ui.SetNextWindowSize({380.0F, 440.0F}, true);
                ui.OpenPopup("VfxGraphNodePalette");
                break;
            case NodeGraphContextTargetKind::Node:
                if (const auto* node = findGraphNode(result.ContextRequested->Node); node)
                {
                    m_ContextNode = node->Id;
                    m_SelectedNode = node->Id;
                    m_SelectedBlock = {};
                    m_SelectedConnection = {};
                    ui.OpenPopup("VfxGraphNodeContext");
                }
                break;
            case NodeGraphContextTargetKind::Block:
                if (const auto block = findGraphBlock({result.ContextRequested->Node, result.ContextRequested->Block});
                    block)
                {
                    m_ContextNode = block->first->Id;
                    m_ContextBlock = block->second->Id;
                    m_SelectedNode = block->first->Id;
                    m_SelectedBlock = block->second->Id;
                    m_SelectedConnection = {};
                    ui.OpenPopup("VfxGraphBlockContext");
                }
                break;
            case NodeGraphContextTargetKind::Pin:
                if (const auto pin = findGraphPin(result.ContextRequested->Node, result.ContextRequested->Block,
                                                  result.ContextRequested->Pin);
                    pin)
                {
                    m_ContextNode = pin->Node;
                    m_ContextBlock = pin->Block;
                    m_ContextPin = pin->Pin;
                    m_SelectedNode = pin->Node;
                    m_SelectedBlock = pin->Block;
                    m_SelectedConnection = {};
                    ui.OpenPopup("VfxGraphPinContext");
                }
                break;
            case NodeGraphContextTargetKind::Connection:
                if (const auto* connection = findGraphConnection(result.ContextRequested->Connection); connection)
                {
                    m_ContextConnection = connection->Id;
                    m_SelectedConnection = connection->Id;
                    m_SelectedNode = {};
                    m_SelectedBlock = {};
                    ui.OpenPopup("VfxGraphConnectionContext");
                }
                break;
            case NodeGraphContextTargetKind::Comment:
                break;
            }
        }
        if (auto popup = ui.BeginPopup("VfxGraphNodePalette"); popup)
        {
            ui.TextColored(theme.Accent, "CREATE NODE");
            ui.TextColored(theme.MutedText, "Search contexts, Runtime Modules, Blackboard properties, and operators.");
            if (m_NodeMenuSelection.ConsumeFocusRequest())
                ui.RequestKeyboardFocus();
            (void)ui.InputTextWithHint("##VfxContextNodeSearch", "Search nodes...", m_NodePaletteSearch);
            ui.Separator();
            if (auto disabled = ui.BeginDisabled(definition.ExecutionSource != Keire::VfxExecutionSource::Graph);
                disabled)
            {
                if (DrawNodePaletteEntries(ui, system->Id, m_NodePalettePosition, m_NodePaletteSearch))
                    return;
            }
            if (definition.ExecutionSource != Keire::VfxExecutionSource::Graph)
                ui.TextColored(theme.Warning, "Convert Runtime Modules to Graph before adding executable nodes.");
            ui.Separator();
            if (ui.MenuItem("Create Empty Comment"))
            {
                (void)CreateGraphComment(ui, system->Id, nodeIdentities, nodes, m_NodePalettePosition, false);
                return;
            }
            if (ui.MenuItem("Frame All Nodes"))
                m_GraphCanvas.Focus(nodes, renderedCanvasSize);
        }
        if (auto popup = ui.BeginPopup("VfxGraphNodeContext"); popup)
        {
            const auto node = std::ranges::find(system->Nodes, m_ContextNode, &Keire::VfxGraphNode::Id);
            if (node != system->Nodes.end())
            {
                ui.TextColored(NodeColor(*node), NodeLabel(definition, *node));
                ui.TextColored(theme.MutedText, std::string(VfxGraphNodeKindLabel(node->Kind)));
                ui.Separator();
                if (ui.MenuItem("Inspect Node"))
                {
                    m_SelectedNode = node->Id;
                    m_SelectedBlock = {};
                    m_SelectedConnection = {};
                }
                if (DrawGraphClipboardContextMenu(ui, nodeIdentities, true,
                                                  node->Kind != Keire::VfxGraphNodeKind::Context))
                    return;
                if (node->Kind == Keire::VfxGraphNodeKind::Context)
                {
                    if (auto addBlockMenu = ui.BeginMenu("Add Compatible Block"); addBlockMenu)
                    {
                        (void)ui.InputTextWithHint("##VfxContextBlockSearch", "Search Blocks...", m_NodePaletteSearch);
                        ui.Separator();
                        if (DrawNodePaletteEntries(ui, system->Id, node->EditorPosition, m_NodePaletteSearch, node->Id))
                        {
                            return;
                        }
                    }
                    ui.Separator();
                }
                if (DrawGraphNodeUnlinkContextMenu(ui, system->Id, node->Id, system->Connections))
                    return;
                if (ui.MenuItem("Create Comment from Selection"))
                {
                    (void)CreateGraphComment(ui, system->Id, nodeIdentities, nodes, node->EditorPosition, true);
                    return;
                }
                std::vector<Keire::AssetId> removableNodes;
                for (const auto selected : m_SelectedNodes)
                {
                    const auto selectedNode = std::ranges::find(system->Nodes, selected, &Keire::VfxGraphNode::Id);
                    if (selectedNode != system->Nodes.end() && selectedNode->Kind != Keire::VfxGraphNodeKind::Context)
                        removableNodes.push_back(selected);
                }
                if (removableNodes.empty() && node->Kind != Keire::VfxGraphNodeKind::Context)
                    removableNodes.push_back(node->Id);
                if (ui.MenuItem(m_SelectedNodes.size() > 1 ? "Delete Nodes" : "Delete Node", false,
                                !removableNodes.empty()))
                {
                    (void)ApplyAction(removableNodes.size() == 1 ? "Removed VFX graph node" : "Removed VFX graph nodes",
                                      [&document, graph = system->Id, removableNodes]
                                      { return document.RemoveNodes(graph, removableNodes); });
                    m_SelectedNode = {};
                    m_SelectedNodes.clear();
                    m_SelectedBlock = {};
                    m_SelectedConnection = {};
                    m_GraphCanvas.Select(std::nullopt);
                    return;
                }
                if (node->Kind == Keire::VfxGraphNodeKind::Context)
                    ui.TextColored(theme.MutedText, "Executable context nodes are fixed graph stages.");
            }
        }
        if (auto popup = ui.BeginPopup("VfxGraphBlockContext"); popup)
        {
            const auto node = std::ranges::find(system->Nodes, m_ContextNode, &Keire::VfxGraphNode::Id);
            if (node != system->Nodes.end())
            {
                const auto block = std::ranges::find(node->Blocks, m_ContextBlock, &Keire::VfxGraphBlock::Id);
                if (block != node->Blocks.end())
                {
                    ui.TextColored(NodeColor(*node), block->Type);
                    ui.TextColored(theme.MutedText,
                                   std::string(EnumName(node->Context, ContextTypes)) + " Context Block");
                    ui.Separator();
                    if (ui.MenuItem("Inspect Block"))
                    {
                        m_SelectedNode = node->Id;
                        m_SelectedBlock = block->Id;
                        m_SelectedConnection = {};
                    }
                    if (ui.MenuItem(block->Enabled ? "Disable Block" : "Enable Block") &&
                        ApplyAction(block->Enabled ? "Disabled VFX Context Block" : "Enabled VFX Context Block",
                                    [&document, graph = system->Id, context = node->Id, blockId = block->Id,
                                     enabled = !block->Enabled]
                                    { return document.SetBlockEnabled(graph, context, blockId, enabled); }))
                    {
                        return;
                    }
                    const auto index = static_cast<std::size_t>(std::distance(node->Blocks.begin(), block));
                    if (ui.MenuItem("Move Up", false, index > 0) &&
                        ApplyAction("Moved VFX Context Block up",
                                    [&document, graph = system->Id, context = node->Id, blockId = block->Id, index]
                                    { return document.MoveBlock(graph, context, blockId, index - 1); }))
                    {
                        return;
                    }
                    if (ui.MenuItem("Move Down", false, index + 1 < node->Blocks.size()) &&
                        ApplyAction("Moved VFX Context Block down",
                                    [&document, graph = system->Id, context = node->Id, blockId = block->Id, index]
                                    { return document.MoveBlock(graph, context, blockId, index + 1); }))
                    {
                        return;
                    }
                    ui.Separator();
                    const bool connected = std::ranges::any_of(
                        system->Connections,
                        [&](const Keire::VfxGraphConnection& connection)
                        {
                            return (connection.OutputNode == node->Id && connection.OutputBlock == block->Id) ||
                                   (connection.InputNode == node->Id && connection.InputBlock == block->Id);
                        });
                    if (ui.MenuItem("Unlink Block Cables", false, connected))
                    {
                        const auto contextId = node->Id;
                        const auto blockId = block->Id;
                        (void)ApplyEdit("Unlinked VFX Context Block",
                                        [graph = system->Id, contextId, blockId](Keire::VfxEffectDefinition& candidate)
                                        {
                                            auto graphSystem =
                                                std::ranges::find(candidate.Systems, graph, &Keire::VfxGraphSystem::Id);
                                            if (graphSystem == candidate.Systems.end())
                                                throw std::invalid_argument("VFX graph system is unavailable.");
                                            std::erase_if(
                                                graphSystem->Connections,
                                                [contextId, blockId](const Keire::VfxGraphConnection& connection)
                                                {
                                                    return (connection.OutputNode == contextId &&
                                                            connection.OutputBlock == blockId) ||
                                                           (connection.InputNode == contextId &&
                                                            connection.InputBlock == blockId);
                                                });
                                        });
                        return;
                    }
                    if (ui.MenuItem("Remove Block") &&
                        ApplyAction("Removed VFX Context Block",
                                    [&document, graph = system->Id, context = node->Id, blockId = block->Id]
                                    { return document.RemoveBlock(graph, context, blockId); }))
                    {
                        m_SelectedBlock = {};
                        m_SelectedConnection = {};
                        m_GraphCanvas.SelectBlock(std::nullopt);
                        m_GraphCanvas.SelectConnection(std::nullopt);
                        return;
                    }
                }
            }
        }

        if (auto popup = ui.BeginPopup("VfxGraphPinContext"); popup)
        {
            const auto node = std::ranges::find(system->Nodes, m_ContextNode, &Keire::VfxGraphNode::Id);
            const Keire::VfxGraphBlock* ownerBlock = nullptr;
            const Keire::VfxGraphPin* pin = nullptr;
            if (node != system->Nodes.end())
            {
                if (m_ContextBlock)
                {
                    const auto block = std::ranges::find(node->Blocks, m_ContextBlock, &Keire::VfxGraphBlock::Id);
                    if (block != node->Blocks.end())
                    {
                        ownerBlock = std::addressof(*block);
                        const auto found = std::ranges::find(block->Pins, m_ContextPin, &Keire::VfxGraphPin::Id);
                        if (found != block->Pins.end())
                            pin = std::addressof(*found);
                    }
                }
                else
                {
                    const auto found = std::ranges::find(node->Pins, m_ContextPin, &Keire::VfxGraphPin::Id);
                    if (found != node->Pins.end())
                        pin = std::addressof(*found);
                }
            }
            if (pin)
            {
                ui.TextColored(PinColor(pin->Type), pin->Name);
                ui.TextColored(theme.MutedText, std::string(pin->Input ? "INPUT  |  " : "OUTPUT  |  ") +
                                                    std::string(EnumName(pin->Type, GraphValueTypes)));
                if (ownerBlock)
                    ui.TextColored(theme.MutedText, "Block: " + ownerBlock->Type);
                ui.Separator();
                const bool connected = std::ranges::any_of(
                    system->Connections,
                    [&](const Keire::VfxGraphConnection& connection)
                    {
                        return (connection.OutputNode == node->Id && connection.OutputBlock == m_ContextBlock &&
                                connection.OutputPin == pin->Id) ||
                               (connection.InputNode == node->Id && connection.InputBlock == m_ContextBlock &&
                                connection.InputPin == pin->Id);
                    });
                if (ui.MenuItem("Unlink Pin", false, connected))
                {
                    const auto nodeId = node->Id;
                    const auto blockId = m_ContextBlock;
                    const auto pinId = pin->Id;
                    (void)ApplyEdit(
                        "Unlinked VFX graph pin",
                        [graph = system->Id, nodeId, blockId, pinId](Keire::VfxEffectDefinition& candidate)
                        {
                            auto graphSystem = std::ranges::find(candidate.Systems, graph, &Keire::VfxGraphSystem::Id);
                            if (graphSystem == candidate.Systems.end())
                                throw std::invalid_argument("VFX graph system is unavailable.");
                            std::erase_if(graphSystem->Connections,
                                          [nodeId, blockId, pinId](const Keire::VfxGraphConnection& connection)
                                          {
                                              return (connection.OutputNode == nodeId &&
                                                      connection.OutputBlock == blockId &&
                                                      connection.OutputPin == pinId) ||
                                                     (connection.InputNode == nodeId &&
                                                      connection.InputBlock == blockId && connection.InputPin == pinId);
                                          });
                        });
                    return;
                }
                if (ownerBlock && ownerBlock->TypeId.View() == "keire.block.portable-hlsl" &&
                    ui.MenuItem("Remove Data Input") &&
                    ApplyAction("Removed Portable HLSL Block input",
                                [&document, graph = system->Id, context = node->Id, blockId = ownerBlock->Id,
                                 pinId = pin->Id] { return document.RemoveBlockPin(graph, context, blockId, pinId); }))
                {
                    m_ContextPin = {};
                    return;
                }
            }
        }

        if (auto popup = ui.BeginPopup("VfxGraphConnectionContext"); popup)
        {
            const auto connection =
                std::ranges::find(system->Connections, m_ContextConnection, &Keire::VfxGraphConnection::Id);
            if (connection != system->Connections.end())
            {
                ui.TextColored(theme.Accent, "GRAPH CABLE");
                ui.TextColored(theme.MutedText,
                               connection->OutputNode.ToString() + " -> " + connection->InputNode.ToString());
                ui.Separator();
                if (ui.MenuItem("Select Source Node"))
                {
                    m_SelectedNode = connection->OutputNode;
                    m_SelectedBlock = connection->OutputBlock;
                    m_SelectedConnection = {};
                }
                if (ui.MenuItem("Select Target Node"))
                {
                    m_SelectedNode = connection->InputNode;
                    m_SelectedBlock = connection->InputBlock;
                    m_SelectedConnection = {};
                }
                ui.Separator();
                if (ui.MenuItem("Unlink Cable"))
                {
                    (void)ApplyAction("Unlinked VFX graph cable",
                                      [&document, graph = system->Id, cable = connection->Id]
                                      { return document.RemoveConnection(graph, cable); });
                    m_SelectedConnection = {};
                    return;
                }
            }
        }

        if (result.BlockMoveRequested)
        {
            if (const auto block = findGraphBlock({result.BlockMoveRequested->Node, result.BlockMoveRequested->Block});
                block)
            {
                (void)ApplyAction("Reordered VFX Context Block",
                                  [&document, graph = system->Id, context = block->first->Id,
                                   blockId = block->second->Id, destination = result.BlockMoveRequested->Destination]
                                  { return document.MoveBlock(graph, context, blockId, destination); });
                return;
            }
        }

        if (!result.MoveCompletedNodes.empty())
        {
            std::vector<std::pair<Keire::AssetId, Keire::Vector2>> moves;
            for (const auto moved : result.MoveCompletedNodes)
                if (const auto* graphNode = findGraphNode(moved);
                    graphNode && std::ranges::find(nodes, moved, &NodeGraphNode::Id) != nodes.end())
                    moves.emplace_back(graphNode->Id, std::ranges::find(nodes, moved, &NodeGraphNode::Id)->Position);
            (void)ApplyAction(moves.size() == 1 ? "Moved VFX graph node" : "Moved VFX graph nodes",
                              [&document, graph = system->Id, moves] { return document.MoveNodes(graph, moves); });
        }
    }

} // namespace KeireEditor
