#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorSessionState.h"

void EditorWorkspaceLayer::PersistEditorSessionScene(const Keire::AssetId asset) noexcept
{
    if (!asset || m_EditorSessionPath.empty())
        return;
    auto state = KeireEditor::LoadEditorSessionState(m_EditorSessionPath);
    state.LastScene = asset;
    state.MaximizeGameOnPlay = m_MaximizeGameOnPlay;
    if (!KeireEditor::SaveEditorSessionState(m_EditorSessionPath, state))
        KEIRE_CLIENT_WARN("[Scene] Could not persist the last open scene for this project.");
}

void EditorWorkspaceLayer::PersistEditorSessionPreferences() noexcept
{
    if (m_EditorSessionPath.empty())
        return;
    if (!KeireEditor::SaveEditorSessionViewPreference(m_EditorSessionPath, m_MaximizeGameOnPlay))
        KEIRE_CLIENT_WARN("[Scene] Could not persist editor session preferences for this project.");
}
