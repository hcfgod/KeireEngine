#include "KeireClient/Editor/VfxEffectPanel.h"

namespace KeireEditor
{
    void VfxEffectPanel::ResetTransientState() noexcept
    {
        m_SelectedModule = {};
        m_SelectedSystem = {};
        m_SelectedNode = {};
        m_SelectedBlock = {};
        m_SelectedConnection = {};
        m_SelectedParameter = {};
        m_ContextNode = {};
        m_ContextBlock = {};
        m_ContextPin = {};
        m_ContextConnection = {};
        m_NodePalettePosition = {};
        m_NodePaletteSearch.clear();
        m_SelectedNodes.clear();
        m_GraphBookmarks.Clear();
        m_InspectorCommentNode = {};
        m_InspectorComment.clear();
        m_InspectorCommentPinned = false;
        m_GraphCanvas.CancelInteractions();
        m_GraphCanvas.Select(std::nullopt);
        m_GraphCanvas.SelectBlock(std::nullopt);
        m_GraphCanvas.SelectConnection(std::nullopt);
        m_AssetPicker.Clear();
        m_Message.clear();
    }

    void VfxEffectPanel::StopTransientPreview() noexcept
    {
        m_GraphCanvas.CancelInteractions();
        m_Controller.StopVfxEffectPreview();
        m_WasVisible = false;
    }
} // namespace KeireEditor
