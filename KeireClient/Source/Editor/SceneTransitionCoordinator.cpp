#include "KeireClient/Editor/SceneTransitionCoordinator.h"

#include <utility>

namespace KeireEditor
{
    bool SceneTransitionCoordinator::Request(SceneTransitionRequest request)
    {
        if (m_Request || m_State == SceneTransitionState::Committing)
            return false;
        m_Request = request;
        m_State = SceneTransitionState::Queued;
        m_Diagnostic.clear();
        return true;
    }

    std::optional<SceneTransitionRequest> SceneTransitionCoordinator::BeginCommit()
    {
        if (!m_Request || m_State != SceneTransitionState::Queued)
            return std::nullopt;
        m_State = SceneTransitionState::Committing;
        return std::exchange(m_Request, std::nullopt);
    }

    void SceneTransitionCoordinator::Complete() noexcept
    {
        m_Request.reset();
        m_State = SceneTransitionState::Idle;
        m_Diagnostic.clear();
    }

    void SceneTransitionCoordinator::Fail(std::string diagnostic) noexcept
    {
        m_Request.reset();
        m_State = SceneTransitionState::Failed;
        m_Diagnostic = std::move(diagnostic);
    }

    void SceneTransitionCoordinator::Cancel() noexcept
    {
        m_Request.reset();
        m_State = SceneTransitionState::Idle;
        m_Diagnostic.clear();
    }

    bool SceneTransitionCoordinator::Pending() const noexcept
    {
        return m_Request.has_value() || m_State == SceneTransitionState::Committing;
    }

    SceneTransitionState SceneTransitionCoordinator::State() const noexcept { return m_State; }

    std::string_view SceneTransitionCoordinator::Diagnostic() const noexcept { return m_Diagnostic; }
} // namespace KeireEditor
