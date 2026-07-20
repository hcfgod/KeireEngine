#include "KeireClient/Editor/SceneDocument.h"

namespace KeireEditor
{
    void SceneDocument::Select(const Keire::AssetId selection) noexcept
    {
        if (!selection || (m_Scene && m_Scene->FindEntity(Keire::EntityId(selection))))
            m_Selection = selection;
        else
            m_Selection = {};
    }

    void SceneDocument::Close() noexcept
    {
        if (m_PlaySession)
            m_PlaySession->Stop();
        m_PlaySession.Reset();
        m_LoadOperation.Reset();
        m_SaveDialog.Reset();
        if (m_Scene)
            m_Scene->Close();
        m_Scene.Reset();
        m_Undo.Reset();
        m_Asset = {};
        m_Selection = {};
        m_Source.clear();
        m_RecoveryPath.clear();
        m_Status.clear();
        m_RecoverySeconds = 0.0;
        m_RecoveryAvailable = false;
    }
} // namespace KeireEditor
