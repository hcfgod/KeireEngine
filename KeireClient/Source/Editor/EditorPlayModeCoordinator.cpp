#include "KeireClient/Editor/EditorPlayModeCoordinator.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    EditorPlayModeCoordinator::EditorPlayModeCoordinator(EditorPlayModeDependencies dependencies)
        : m_Dependencies(std::move(dependencies))
    {
        if (!m_Dependencies.ProcessSceneTransition || !m_Dependencies.FinalizeEditorMutation ||
            !m_Dependencies.CompletePendingTransition || !m_Dependencies.UpdateRuntime ||
            !m_Dependencies.ContinuePendingPlay || !m_Dependencies.CloseRuntime)
        {
            throw std::invalid_argument("Play-mode coordinator dependencies must be callable.");
        }
    }

    EditorPlayModeCoordinator::~EditorPlayModeCoordinator() noexcept { Shutdown(); }

    void EditorPlayModeCoordinator::UpdateTransitions()
    {
        m_Lifetime.RequireOwnerThread("UpdateTransitions");
        m_Dependencies.ProcessSceneTransition();
        m_Dependencies.FinalizeEditorMutation();
        m_Dependencies.CompletePendingTransition();
    }

    void EditorPlayModeCoordinator::UpdateRuntime(const double deltaSeconds, const double interpolationAlpha)
    {
        m_Lifetime.RequireOwnerThread("UpdateRuntime");
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 || !std::isfinite(interpolationAlpha))
            throw std::invalid_argument("Play-mode update timing must be finite and non-negative.");
        m_Dependencies.UpdateRuntime(deltaSeconds, interpolationAlpha);
    }

    void EditorPlayModeCoordinator::ContinuePendingPlay()
    {
        m_Lifetime.RequireOwnerThread("ContinuePendingPlay");
        m_Dependencies.ContinuePendingPlay();
    }

    void EditorPlayModeCoordinator::Shutdown() noexcept
    {
        if (!m_Lifetime.BeginShutdown())
            return;
        try
        {
            m_Dependencies.CloseRuntime();
        }
        catch (...)
        {
            if (!m_Dependencies.ReportShutdownFailure)
                return;
            try
            {
                m_Dependencies.ReportShutdownFailure("close-runtime", std::current_exception());
            }
            catch (...)
            {
            }
        }
    }

    EditorWorkspaceCallbackToken EditorPlayModeCoordinator::CaptureCallbackToken() const
    {
        return m_Lifetime.CaptureCallbackToken();
    }

    bool EditorPlayModeCoordinator::ShutdownComplete() const { return m_Lifetime.ShutdownComplete(); }
} // namespace KeireEditor
