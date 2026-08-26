#include "KeireClient/EditorWorkspaceLayer.h"

#include "KeireClient/Editor/SceneDocument.h"
#include "KeireClient/Editor/ScenePlayChanges.h"

#include <algorithm>
#include <ranges>
#include <utility>

void EditorWorkspaceLayer::CompletePendingPlayTransition()
{
    if (m_PendingPlayTransition == PendingPlayTransition::None)
        return;
    const auto transition = std::exchange(m_PendingPlayTransition, PendingPlayTransition::None);
    FinishPlayMode(transition == PendingPlayTransition::Apply);
}

void EditorWorkspaceLayer::UpdatePlayRuntime(const double deltaSeconds, const double interpolationAlpha)
{
    if (!m_PlayRuntimeWorld)
        return;

    Keire::ProfileScope playUpdate(Owner().GetProfiler(), Keire::ProfileCategory::Scripting, "Play update");
    m_PlayRuntimeWorld->Process();
    const auto active = m_PlayRuntimeWorld->Session(m_PlayRuntimeWorld->Active());
    if (active && active != m_SceneDocument->PlaySession())
    {
        if (m_PlayChangeTracker)
            m_PlayChangeTracker->BindSession(active);
        m_PlayEditorTouchedEntities.clear();
        m_PendingPlayEditorBefore.reset();
        m_SceneDocument->SetPlaySession(active);
    }
    m_PlayRuntimeWorld->Update(static_cast<float>(deltaSeconds), static_cast<float>(interpolationAlpha));
    const auto sessions = m_PlayRuntimeWorld->Sessions();
    const auto faulted = std::ranges::find_if(sessions, [](const auto& session)
                                              { return session->State() == Keire::ScenePlayState::Faulted; });
    if (faulted == sessions.end() || m_PlayFaultReported)
        return;

    const auto diagnostic = (*faulted)->Diagnostic();
    m_SceneDocument->SetStatus(diagnostic.Callback + " failed: " + diagnostic.Message);
    ReportError("Play Mode", m_SceneDocument->Status());
    m_PlayFaultReported = true;
}
