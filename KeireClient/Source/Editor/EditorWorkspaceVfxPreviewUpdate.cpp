#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/SceneDocument.h"

#include <algorithm>
#include <exception>
#include <string>

void EditorWorkspaceLayer::UpdateEditModeVfxPreviews(const double elapsedSeconds)
{
    if (m_SceneDocument->PlaySession())
    {
        StopEditModeVfxPreviews();
        return;
    }

    try
    {
        SynchronizeEditModeVfxPreviews();
    }
    catch (const std::exception& error)
    {
        StopEditModeVfxPreviews();
        ReportError("VFX", std::string("Edit-mode VFX preview synchronization failed: ") + error.what());
    }

    if (!m_VfxEffectPreviewWorld)
    {
        return;
    }

    const auto deltaSeconds = static_cast<float>(std::clamp(elapsedSeconds, 0.0, 0.1));
    m_VfxEffectPreviewWorld->Update(deltaSeconds);
    if (m_VfxEffectPreviewAutoRestart && m_VfxEffectPreviewEffect &&
        !m_VfxEffectPreviewWorld->IsAlive(m_VfxEffectPreviewHandle))
    {
        m_VfxEffectPreviewHandle = m_VfxEffectPreviewWorld->Activate(
            {m_VfxEffectPreviewEffect, m_VfxEffectPreviewRevision, m_VfxEffectPreviewPosition,
             m_VfxEffectPreviewRotation, m_VfxEffectPreviewSeedOffset});
        if (m_VfxEffectPreviewHandle)
        {
            m_VfxEffectPreviewWorld->SetSimulationSpeed(m_VfxEffectPreviewHandle,
                                                        m_VfxEffectPreviewPaused ? 0.0F : m_VfxEffectPreviewSpeed);
        }
    }
}
