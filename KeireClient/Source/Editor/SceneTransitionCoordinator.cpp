#include "KeireClient/Editor/SceneTransitionCoordinator.h"

#include <utility>

namespace KeireEditor
{
    bool SceneTransitionCoordinator::Request(SceneTransitionRequest request)
    {
        if (m_Request || m_State == SceneTransitionState::Committing)
            return false;
        // A user transition supersedes the startup intent even if its later load fails or is cancelled.
        m_StartupScene = {};
        m_AwaitingSceneAssets = {};
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

    void SceneTransitionCoordinator::DeferStartupScene(const Keire::AssetId asset) noexcept
    {
        if (!Pending())
        {
            m_StartupScene = asset;
        }
    }

    Keire::AssetId SceneTransitionCoordinator::PendingStartupScene() const noexcept { return m_StartupScene; }

    Keire::AssetId SceneTransitionCoordinator::TakeStartupScene() noexcept { return std::exchange(m_StartupScene, {}); }

    void SceneTransitionCoordinator::AwaitSceneAssets(const Keire::AssetId asset) noexcept
    {
        m_AwaitingSceneAssets = asset;
    }

    Keire::AssetId SceneTransitionCoordinator::AwaitingSceneAssets() const noexcept { return m_AwaitingSceneAssets; }

    bool SceneTransitionCoordinator::CompleteSceneAssets(const Keire::AssetId asset) noexcept
    {
        if (!asset || asset != m_AwaitingSceneAssets)
        {
            return false;
        }
        m_AwaitingSceneAssets = {};
        return true;
    }

    void SceneTransitionCoordinator::RequireStartupRefresh(const Keire::AssetId asset) noexcept
    {
        m_StartupRefresh = asset;
    }

    Keire::AssetId SceneTransitionCoordinator::StartupRefresh() const noexcept { return m_StartupRefresh; }

    void SceneTransitionCoordinator::CompleteStartupRefresh(const Keire::AssetId asset) noexcept
    {
        if (asset == m_StartupRefresh)
        {
            m_StartupRefresh = {};
        }
    }

    SceneTransitionState SceneTransitionCoordinator::State() const noexcept { return m_State; }

    std::string_view SceneTransitionCoordinator::Diagnostic() const noexcept { return m_Diagnostic; }
} // namespace KeireEditor
