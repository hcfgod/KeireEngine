#include "KeireClient/Editor/GraphDuplication.h"
#include "KeireClient/Editor/MaterialGraphPanel.h"
#include "KeireClient/Editor/ShaderGraphPanel.h"
#include "KeireClient/Editor/VfxEffectDocument.h"
#include "KeireClient/Editor/VfxEffectPanel.h"

#include <exception>

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
            }
            if (result.PasteRequested)
            {
                std::vector<Keire::AssetId> pasted;
                (void)m_Controller.ShaderGraphState().Edit(
                    "Paste Shader Graph fragment", [&](auto& definition)
                    { pasted = PasteShaderGraphFragment(definition, m_Controller.GraphClipboard()); });
                m_SelectedNodes = pasted;
                m_SelectedNode = pasted.back();
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
            }
            if (result.PasteRequested)
            {
                std::vector<Keire::AssetId> pasted;
                (void)m_Controller.MaterialGraphState().Edit(
                    "Paste Material Graph fragment", [&](auto& definition)
                    { pasted = PasteMaterialGraphFragment(definition, m_Controller.GraphClipboard()); });
                m_SelectedNodes = pasted;
                m_SelectedNode = pasted.back();
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
            }
            if (result.PasteRequested)
            {
                std::vector<Keire::AssetId> pasted;
                (void)m_Controller.VfxEffectState().Edit(
                    "Paste VFX Graph fragment", [&](auto& definition)
                    { pasted = PasteVfxGraphFragment(definition, m_SelectedSystem, m_Controller.GraphClipboard()); });
                m_SelectedNodes = pasted;
                m_SelectedNode = pasted.back();
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
