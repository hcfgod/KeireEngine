#include "KeireClient/Editor/MaterialGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        struct ArrangeRequest
        {
            std::optional<GraphAlignment> Alignment;
            std::optional<GraphDistribution> Distribution;
            bool Straighten = false;
        };

        [[nodiscard]] ArrangeRequest DrawRequest(Keire::UiFrame& ui, const std::size_t selectionCount)
        {
            ArrangeRequest result;
            if (auto menu = ui.BeginCombo("Arrange", "Selection..."); menu)
            {
                if (auto disabled = ui.BeginDisabled(selectionCount < 2); disabled)
                {
                    if (ui.Selectable("Align Left"))
                        result.Alignment = GraphAlignment::Left;
                    if (ui.Selectable("Align Centers Horizontally"))
                        result.Alignment = GraphAlignment::HorizontalCenter;
                    if (ui.Selectable("Align Right"))
                        result.Alignment = GraphAlignment::Right;
                    if (ui.Selectable("Align Top"))
                        result.Alignment = GraphAlignment::Top;
                    if (ui.Selectable("Align Centers Vertically"))
                        result.Alignment = GraphAlignment::VerticalCenter;
                    if (ui.Selectable("Align Bottom"))
                        result.Alignment = GraphAlignment::Bottom;
                }
                ui.Separator();
                if (auto disabled = ui.BeginDisabled(selectionCount < 3); disabled)
                {
                    if (ui.Selectable("Distribute Horizontally"))
                        result.Distribution = GraphDistribution::Horizontal;
                    if (ui.Selectable("Distribute Vertically"))
                        result.Distribution = GraphDistribution::Vertical;
                }
                ui.Separator();
                if (auto disabled = ui.BeginDisabled(selectionCount < 2); disabled)
                    result.Straighten = ui.Selectable("Straighten Internal Cables");
            }
            return result;
        }

        [[nodiscard]] std::vector<StableNodeId>
        CanvasSelection(const std::span<const Keire::AssetId> selection,
                        const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
        {
            std::vector<StableNodeId> result;
            for (const auto id : selection)
                if (const auto found = std::ranges::find(identities, id, &decltype(identities)::value_type::second);
                    found != identities.end())
                    result.push_back(found->first);
            return result;
        }

        [[nodiscard]] std::vector<std::pair<Keire::AssetId, Keire::Vector2>>
        ResolveMoves(const std::span<const std::pair<StableNodeId, Keire::Vector2>> moves,
                     const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
        {
            std::vector<std::pair<Keire::AssetId, Keire::Vector2>> result;
            for (const auto& [canvas, position] : moves)
                if (const auto found = std::ranges::find(identities, canvas, &decltype(identities)::value_type::first);
                    found != identities.end())
                    result.emplace_back(found->second, position);
            return result;
        }

        [[nodiscard]] std::vector<Keire::AssetId>
        ResolveConnections(const std::span<const StableNodeId> connections,
                           const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
        {
            std::vector<Keire::AssetId> result;
            for (const auto canvas : connections)
                if (const auto found = std::ranges::find(identities, canvas, &decltype(identities)::value_type::first);
                    found != identities.end())
                    result.push_back(found->second);
            return result;
        }

        [[nodiscard]] std::vector<std::pair<StableNodeId, Keire::Vector2>>
        RequestedMoves(const ArrangeRequest& request, const std::span<const NodeGraphNode> nodes,
                       const std::span<const StableNodeId> selection)
        {
            if (request.Alignment)
                return AlignGraphNodes(nodes, selection, *request.Alignment);
            if (request.Distribution)
                return DistributeGraphNodes(nodes, selection, *request.Distribution);
            return {};
        }
    } // namespace

    bool ShaderGraphPanel::DrawArrangeMenu(
        Keire::UiFrame& ui, const std::span<const NodeGraphNode> nodes,
        const std::span<const NodeGraphConnection> connections,
        const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
        const std::span<const std::pair<StableNodeId, Keire::AssetId>> connectionIdentities)
    {
        const auto selected = CanvasSelection(m_SelectedNodes, nodeIdentities);
        const auto request = DrawRequest(ui, selected.size());
        try
        {
            if (request.Alignment || request.Distribution)
                return m_Controller.ShaderGraphState().MoveNodes(
                    ResolveMoves(RequestedMoves(request, nodes, selected), nodeIdentities));
            if (request.Straighten)
            {
                const auto cables =
                    ResolveConnections(InternalGraphConnections(connections, selected), connectionIdentities);
                return m_Controller.ShaderGraphState().Edit("Straighten Shader Graph cables",
                                                            [cables](auto& definition)
                                                            {
                                                                for (auto& cable : definition.Connections)
                                                                    if (std::ranges::find(cables, cable.Id) !=
                                                                        cables.end())
                                                                        cable.RoutingPoints.clear();
                                                            });
            }
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
        return false;
    }

    bool MaterialGraphPanel::DrawArrangeMenu(
        Keire::UiFrame& ui, const std::span<const NodeGraphNode> nodes,
        const std::span<const NodeGraphConnection> connections,
        const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
        const std::span<const std::pair<StableNodeId, Keire::AssetId>> connectionIdentities)
    {
        const auto selected = CanvasSelection(m_SelectedNodes, nodeIdentities);
        const auto request = DrawRequest(ui, selected.size());
        try
        {
            if (request.Alignment || request.Distribution)
                return m_Controller.MaterialGraphState().MoveNodes(
                    ResolveMoves(RequestedMoves(request, nodes, selected), nodeIdentities));
            if (request.Straighten)
            {
                const auto cables =
                    ResolveConnections(InternalGraphConnections(connections, selected), connectionIdentities);
                return m_Controller.MaterialGraphState().Edit("Straighten Material Graph cables",
                                                              [cables](auto& definition)
                                                              {
                                                                  const auto clear = [&](auto& graphConnections)
                                                                  {
                                                                      for (auto& cable : graphConnections)
                                                                          if (std::ranges::find(cables, cable.Id) !=
                                                                              cables.end())
                                                                              cable.RoutingPoints.clear();
                                                                  };
                                                                  clear(definition.Connections);
                                                                  clear(definition.SurfaceGraph.Connections);
                                                              });
            }
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
        return false;
    }

    bool VfxEffectPanel::DrawGraphArrangeMenu(
        Keire::UiFrame& ui, const std::span<const NodeGraphNode> nodes,
        const std::span<const NodeGraphConnection> connections,
        const std::span<const std::pair<StableNodeId, Keire::AssetId>> nodeIdentities,
        const std::span<const std::pair<StableNodeId, Keire::AssetId>> connectionIdentities)
    {
        const auto selected = CanvasSelection(m_SelectedNodes, nodeIdentities);
        const auto request = DrawRequest(ui, selected.size());
        if (request.Alignment || request.Distribution)
            return ApplyAction("Arrange VFX Graph nodes",
                               [&]
                               {
                                   return m_Controller.VfxEffectState().MoveNodes(
                                       m_SelectedSystem,
                                       ResolveMoves(RequestedMoves(request, nodes, selected), nodeIdentities));
                               });
        if (request.Straighten)
        {
            const auto cables =
                ResolveConnections(InternalGraphConnections(connections, selected), connectionIdentities);
            return ApplyEdit("Straighten VFX Graph cables",
                             [&](auto& definition)
                             {
                                 const auto system = std::ranges::find(definition.Systems, m_SelectedSystem,
                                                                       &Keire::VfxGraphSystem::Id);
                                 if (system == definition.Systems.end())
                                     throw std::invalid_argument("VFX graph system is unavailable.");
                                 for (auto& cable : system->Connections)
                                     if (std::ranges::find(cables, cable.Id) != cables.end())
                                         cable.RoutingPoints.clear();
                             });
        }
        return false;
    }
} // namespace KeireEditor
