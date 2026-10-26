#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/ProjectSettingsDocument.h"
#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClientInternal/Editor/EnvironmentPreviewClock.h"
#include "KeireInternal/Environment/SceneRuntimeEnvironmentAccess.h"

#include <exception>
#include <string>

std::optional<Keire::EnvironmentLightingSnapshot> EditorWorkspaceLayer::SceneViewportEnvironmentLighting()
{
    if (m_PlayRuntimeWorld && m_SceneDocument->PlaySession())
    {
        return Keire::Detail::SceneRuntimeEnvironmentAccess::CaptureLighting(*m_PlayRuntimeWorld);
    }
    if (!m_ProjectSettingsDocument->Opened())
    {
        return std::nullopt;
    }
    m_EnvironmentPreview->SynchronizeAuthored(m_ProjectSettingsDocument->EnvironmentTime());
    auto source = m_EnvironmentPreview->LightingSource();
    if (!source)
    {
        source = Keire::AssetId::Generate();
    }
    return m_EnvironmentPreview->CaptureLighting(m_ProjectSettingsDocument->EnvironmentLighting(), source);
}

KeireEditor::EnvironmentPreviewClock* EditorWorkspaceLayer::ProjectEnvironmentPreview()
{
    if (!m_ProjectSettingsDocument->Opened() || m_SceneDocument->PlaySession())
    {
        return nullptr;
    }
    m_EnvironmentPreview->SynchronizeAuthored(m_ProjectSettingsDocument->EnvironmentTime());
    return m_EnvironmentPreview.get();
}

void EditorWorkspaceLayer::UpdateEnvironmentPreview(const double elapsedSeconds)
{
    if (!m_ProjectSettingsDocument->Opened())
    {
        m_EnvironmentPreview->Unbind();
        return;
    }
    try
    {
        m_EnvironmentPreview->SynchronizeAuthored(m_ProjectSettingsDocument->EnvironmentTime());
        const bool visible = m_ProjectSettingsPanel->Registration().Visible() ||
                             m_ProjectSettingsPanel->EnvironmentRegistration().Visible() ||
                             m_SceneViewportPanel->Registration().Visible() || m_Game.Visible();
        m_EnvironmentPreview->Tick(elapsedSeconds, !m_SceneDocument->PlaySession(), visible);
    }
    catch (const std::exception& error)
    {
        if (m_EnvironmentPreview->Bound())
        {
            m_EnvironmentPreview->SetPlaying(false);
        }
        ReportError("Environment preview", std::string("Preview stopped: ") + error.what());
    }
}
