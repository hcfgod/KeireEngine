#include "KeireClient/Editor/EditorBuildCookCoordinator.h"

#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    EditorBuildCookCoordinator::EditorBuildCookCoordinator(EditorBuildCookDependencies dependencies)
        : m_Dependencies(std::move(dependencies))
    {
        if (!m_Dependencies.UpdateBuild || !m_Dependencies.AssetDatabaseReady || !m_Dependencies.ShutdownBuild)
            throw std::invalid_argument("Build-cook coordinator dependencies must be callable.");
    }

    EditorBuildCookCoordinator::~EditorBuildCookCoordinator() noexcept { Shutdown(); }

    bool EditorBuildCookCoordinator::Update()
    {
        m_Lifetime.RequireOwnerThread("Update");
        m_Dependencies.UpdateBuild();
        return m_Dependencies.AssetDatabaseReady();
    }

    void EditorBuildCookCoordinator::Shutdown() noexcept
    {
        if (!m_Lifetime.BeginShutdown())
            return;
        try
        {
            m_Dependencies.ShutdownBuild();
        }
        catch (...)
        {
            if (!m_Dependencies.ReportShutdownFailure)
                return;
            try
            {
                m_Dependencies.ReportShutdownFailure("shutdown-build", std::current_exception());
            }
            catch (...)
            {
            }
        }
    }

    EditorWorkspaceCallbackToken EditorBuildCookCoordinator::CaptureCallbackToken() const
    {
        return m_Lifetime.CaptureCallbackToken();
    }

    bool EditorBuildCookCoordinator::ShutdownComplete() const { return m_Lifetime.ShutdownComplete(); }
} // namespace KeireEditor
