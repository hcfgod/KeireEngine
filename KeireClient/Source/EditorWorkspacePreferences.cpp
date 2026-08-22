#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorSessionState.h"
#include "KeireClient/Editor/SceneDocument.h"

void EditorWorkspaceLayer::PersistEditorSessionScene(const Keire::AssetId asset) noexcept
{
    if (!asset || m_EditorSessionPath.empty())
        return;
    if (!KeireEditor::SaveEditorSessionState(m_EditorSessionPath,
                                             {.LastScene = asset, .MaximizeGameOnPlay = m_MaximizeGameOnPlay}))
        KEIRE_CLIENT_WARN("[Scene] Could not persist the last open scene for this project.");
}

void EditorWorkspaceLayer::PersistEditorSessionPreferences() noexcept
{
    if (m_EditorSessionPath.empty())
        return;
    if (!KeireEditor::SaveEditorSessionState(
            m_EditorSessionPath, {.LastScene = m_SceneDocument ? m_SceneDocument->Asset() : Keire::AssetId{},
                                  .MaximizeGameOnPlay = m_MaximizeGameOnPlay}))
        KEIRE_CLIENT_WARN("[Scene] Could not persist editor session preferences for this project.");
}
