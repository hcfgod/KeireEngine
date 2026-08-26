#include "KeireClient/Editor/EditorReplayProfilingCoordinator.h"

namespace KeireEditor
{
    EditorReplayProfilingCoordinator::~EditorReplayProfilingCoordinator() noexcept { Shutdown(); }

    EditorReplayState& EditorReplayProfilingCoordinator::Replay()
    {
        m_Lifetime.RequireOwnerThread("access replay state");
        return m_Replay;
    }

    EditorProfilerState& EditorReplayProfilingCoordinator::Profiler()
    {
        m_Lifetime.RequireOwnerThread("access profiler state");
        return m_Profiler;
    }

    const EditorReplayState& EditorReplayProfilingCoordinator::Replay() const
    {
        m_Lifetime.RequireOwnerThread("access replay state");
        return m_Replay;
    }

    const EditorProfilerState& EditorReplayProfilingCoordinator::Profiler() const
    {
        m_Lifetime.RequireOwnerThread("access profiler state");
        return m_Profiler;
    }

    void EditorReplayProfilingCoordinator::Shutdown() noexcept
    {
        if (!m_Lifetime.BeginShutdown())
            return;
        m_Replay = {};
        m_Profiler = {};
    }

    EditorWorkspaceCallbackToken EditorReplayProfilingCoordinator::CaptureCallbackToken() const
    {
        return m_Lifetime.CaptureCallbackToken();
    }

    bool EditorReplayProfilingCoordinator::ShutdownComplete() const { return m_Lifetime.ShutdownComplete(); }
} // namespace KeireEditor
