#include "KeireClient/Editor/GraphDuplication.h"
#include "KeireClient/Editor/MaterialGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace KeireEditor
{
    bool ShaderGraphPanel::HandleClipboard(const NodeGraphCanvasResult& result,
                                           const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
    {
        const auto requested = !result.CopyNodesRequested.empty()  ? result.CopyNodesRequested
                               : !result.CutNodesRequested.empty() ? result.CutNodesRequested
                                                                   : std::vector<StableNodeId>{};
        try
        {
            if (!requested.empty())
            {
                const auto selected = ResolveGraphSelection(requested, identities);
                m_Controller.SetGraphClipboard(
                    CopyShaderGraphFragment(m_Controller.ShaderGraphState().Definition(), selected));
                if (!result.CutNodesRequested.empty())
                {
                    (void)m_Controller.ShaderGraphState().RemoveNodes(selected);
                    m_SelectedNode.reset();
                    m_SelectedNodes.clear();
                    m_Canvas.Select(std::nullopt);
                    return true;
                }
                return true;
            }
            if (result.PasteRequested)
            {
                std::vector<Keire::AssetId> pasted;
                (void)m_Controller.ShaderGraphState().Edit(
                    "Paste Shader Graph fragment", [&](auto& definition)
                    { pasted = PasteShaderGraphFragment(definition, m_Controller.GraphClipboard()); });
                m_SelectedNodes = pasted;
                m_SelectedNode = pasted.empty() ? std::nullopt : std::optional(pasted.back());
                m_SelectedConnection.reset();
                return true;
            }
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
        return false;
    }

    bool MaterialGraphPanel::HandleClipboard(const NodeGraphCanvasResult& result,
                                             const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
    {
        const auto requested = !result.CopyNodesRequested.empty()  ? result.CopyNodesRequested
                               : !result.CutNodesRequested.empty() ? result.CutNodesRequested
                                                                   : std::vector<StableNodeId>{};
        try
        {
            if (!requested.empty())
            {
                const auto selected = ResolveGraphSelection(requested, identities);
                m_Controller.SetGraphClipboard(
                    CopyMaterialGraphFragment(m_Controller.MaterialGraphState().Definition(), selected));
                if (!result.CutNodesRequested.empty())
                {
                    (void)m_Controller.MaterialGraphState().RemoveNodes(selected);
                    m_SelectedNode.reset();
                    m_SelectedNodes.clear();
                    m_Canvas.Select(std::nullopt);
                    return true;
                }
                return true;
            }
            if (result.PasteRequested)
            {
                std::vector<Keire::AssetId> pasted;
                (void)m_Controller.MaterialGraphState().Edit(
                    "Paste Material Graph fragment", [&](auto& definition)
                    { pasted = PasteMaterialGraphFragment(definition, m_Controller.GraphClipboard()); });
                m_SelectedNodes = pasted;
                m_SelectedNode = pasted.empty() ? std::nullopt : std::optional(pasted.back());
                m_SelectedConnection.reset();
                return true;
            }
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
        return false;
    }

    bool VfxEffectPanel::HandleGraphClipboard(const NodeGraphCanvasResult& result,
                                              const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
    {
        const auto requested = !result.CopyNodesRequested.empty()  ? result.CopyNodesRequested
                               : !result.CutNodesRequested.empty() ? result.CutNodesRequested
                                                                   : std::vector<StableNodeId>{};
        try
        {
            if (!requested.empty())
            {
                const auto selected = ResolveGraphSelection(requested, identities);
                m_Controller.SetGraphClipboard(
                    CopyVfxGraphFragment(m_Controller.VfxEffectState().Definition(), m_SelectedSystem, selected));
                if (!result.CutNodesRequested.empty())
                {
                    (void)m_Controller.VfxEffectState().RemoveNodes(m_SelectedSystem, selected);
                    m_SelectedNode = {};
                    m_SelectedNodes.clear();
                    m_GraphCanvas.Select(std::nullopt);
                    return true;
                }
                return true;
            }
            if (result.PasteRequested)
            {
                std::vector<Keire::AssetId> pasted;
                (void)m_Controller.VfxEffectState().Edit(
                    "Paste VFX Graph fragment", [&](auto& definition)
                    { pasted = PasteVfxGraphFragment(definition, m_SelectedSystem, m_Controller.GraphClipboard()); });
                m_SelectedNodes = pasted;
                m_SelectedNode = pasted.empty() ? Keire::AssetId{} : pasted.back();
                m_SelectedConnection = {};
                return true;
            }
        }
        catch (const std::exception& error)
        {
            m_Message = error.what();
        }
        return false;
    }

    bool ShaderGraphPanel::DrawClipboardContextMenu(
        Keire::UiFrame& ui, const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
        const bool includeCopy, const bool copyEnabled)
    {
        NodeGraphCanvasResult command;
        if (includeCopy)
        {
            command.CopyNodesRequested.assign(m_Canvas.Selections().begin(), m_Canvas.Selections().end());
            if (ui.MenuItem("Copy", false, copyEnabled) && HandleClipboard(command, identities))
                return true;
            command.CopyNodesRequested.clear();
        }
        command.PasteRequested = true;
        return ui.MenuItem("Paste") && HandleClipboard(command, identities);
    }

    bool MaterialGraphPanel::DrawClipboardContextMenu(
        Keire::UiFrame& ui, const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
        const bool includeCopy, const bool copyEnabled)
    {
        NodeGraphCanvasResult command;
        if (includeCopy)
        {
            command.CopyNodesRequested.assign(m_Canvas.Selections().begin(), m_Canvas.Selections().end());
            if (ui.MenuItem("Copy", false, copyEnabled) && HandleClipboard(command, identities))
                return true;
            command.CopyNodesRequested.clear();
        }
        command.PasteRequested = true;
        return ui.MenuItem("Paste") && HandleClipboard(command, identities);
    }

    bool VfxEffectPanel::DrawGraphClipboardContextMenu(
        Keire::UiFrame& ui, const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities,
        const bool includeCopy, const bool copyEnabled)
    {
        NodeGraphCanvasResult command;
        if (includeCopy)
        {
            command.CopyNodesRequested.assign(m_GraphCanvas.Selections().begin(), m_GraphCanvas.Selections().end());
            if (ui.MenuItem("Copy", false, copyEnabled) && HandleGraphClipboard(command, identities))
                return true;
            command.CopyNodesRequested.clear();
        }
        command.PasteRequested = true;
        return ui.MenuItem("Paste") && HandleGraphClipboard(command, identities);
    }

    bool VfxEffectPanel::DrawGraphNodeUnlinkContextMenu(Keire::UiFrame& ui, const Keire::AssetId system,
                                                        const Keire::AssetId node,
                                                        const std::span<const Keire::VfxGraphConnection> connections)
    {
        const bool connected =
            std::ranges::any_of(connections, [&](const Keire::VfxGraphConnection& connection)
                                { return connection.OutputNode == node || connection.InputNode == node; });
        if (!ui.MenuItem("Unlink All Cables", false, connected))
            return false;
        (void)ApplyEdit("Unlinked all VFX node cables",
                        [system, node](Keire::VfxEffectDefinition& candidate)
                        {
                            auto graph = std::ranges::find(candidate.Systems, system, &Keire::VfxGraphSystem::Id);
                            if (graph == candidate.Systems.end())
                                throw std::invalid_argument("VFX graph system is unavailable.");
                            std::erase_if(graph->Connections, [node](const Keire::VfxGraphConnection& connection)
                                          { return connection.OutputNode == node || connection.InputNode == node; });
                        });
        return true;
    }

    void ShaderGraphPanel::DuplicateSelection(const std::span<const StableNodeId> selection,
                                              const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
    {
        const auto source = ResolveGraphSelection(selection, identities);
        std::vector<Keire::AssetId> duplicated;
        try
        {
            (void)m_Controller.ShaderGraphState().Edit(
                "Duplicate Shader Graph selection",
                [&](auto& definition) { duplicated = DuplicateShaderGraphSelection(definition, source); });
            if (!duplicated.empty())
            {
                m_SelectedNodes = duplicated;
                m_SelectedNode = duplicated.back();
                m_SelectedConnection.reset();
            }
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
    }

    void
    MaterialGraphPanel::DuplicateSelection(const std::span<const StableNodeId> selection,
                                           const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
    {
        const auto source = ResolveGraphSelection(selection, identities);
        std::vector<Keire::AssetId> duplicated;
        try
        {
            (void)m_Controller.MaterialGraphState().Edit(
                "Duplicate Material Graph selection",
                [&](auto& definition) { duplicated = DuplicateMaterialGraphSelection(definition, source); });
            if (!duplicated.empty())
            {
                m_SelectedNodes = duplicated;
                m_SelectedNode = duplicated.back();
                m_SelectedConnection.reset();
            }
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
    }

    void
    VfxEffectPanel::DuplicateGraphSelection(const std::span<const StableNodeId> selection,
                                            const std::span<const std::pair<StableNodeId, Keire::AssetId>> identities)
    {
        const auto source = ResolveGraphSelection(selection, identities);
        std::vector<Keire::AssetId> duplicated;
        try
        {
            (void)m_Controller.VfxEffectState().Edit(
                "Duplicate VFX Graph selection", [&](auto& definition)
                { duplicated = DuplicateVfxGraphSelection(definition, m_SelectedSystem, source); });
            if (!duplicated.empty())
            {
                m_SelectedNodes = duplicated;
                m_SelectedNode = duplicated.back();
                m_SelectedConnection = {};
            }
        }
        catch (const std::exception& error)
        {
            m_Message = error.what();
        }
    }
} // namespace KeireEditor
