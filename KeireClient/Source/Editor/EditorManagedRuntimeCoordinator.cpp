#include "KeireClient/Editor/EditorManagedRuntimeCoordinator.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    EditorManagedRuntimeCoordinator::EditorManagedRuntimeCoordinator(EditorManagedRuntimeDependencies dependencies)
        : m_Dependencies(std::move(dependencies))
    {
        if (!m_Dependencies.StartBuild || !m_Dependencies.PollBuild || !m_Dependencies.ReportBuildError ||
            !m_Dependencies.DetachRuntimeServices || !m_Dependencies.ResetRuntimeInput)
        {
            throw std::invalid_argument("Managed-runtime coordinator dependencies must be callable.");
        }
    }

    EditorManagedRuntimeCoordinator::~EditorManagedRuntimeCoordinator() noexcept { Shutdown(); }

    void EditorManagedRuntimeCoordinator::ScheduleBuild(const double delaySeconds)
    {
        m_Lifetime.RequireOwnerThread("ScheduleBuild");
        if (!std::isfinite(delaySeconds) || delaySeconds < 0.0)
            throw std::invalid_argument("Managed build delay must be finite and non-negative.");
        m_ScheduledBuildDelaySeconds = delaySeconds;
        m_BuildDelayElapsedSeconds = 0.0;
    }

    void EditorManagedRuntimeCoordinator::Update(const double unscaledDeltaSeconds)
    {
        m_Lifetime.RequireOwnerThread("Update");
        if (!std::isfinite(unscaledDeltaSeconds) || unscaledDeltaSeconds < 0.0)
            throw std::invalid_argument("Managed-runtime update delta must be finite and non-negative.");

        if (m_ScheduledBuildDelaySeconds >= 0.0)
        {
            m_BuildDelayElapsedSeconds += unscaledDeltaSeconds;
            if (m_BuildDelayElapsedSeconds >= m_ScheduledBuildDelaySeconds)
            {
                m_ScheduledBuildDelaySeconds = -1.0;
                m_BuildDelayElapsedSeconds = 0.0;
                try
                {
                    m_Dependencies.StartBuild();
                }
                catch (const std::exception& error)
                {
                    m_Dependencies.ReportBuildError(error.what());
                }
                catch (...)
                {
                    m_Dependencies.ReportBuildError("Managed build failed with a non-standard exception.");
                }
            }
        }
        m_Dependencies.PollBuild();
    }

    void EditorManagedRuntimeCoordinator::DetachRuntimeServices() noexcept
    {
        if (m_RuntimeServicesDetached)
            return;
        try
        {
            m_Lifetime.RequireOwnerThread("DetachRuntimeServices");
            m_Dependencies.DetachRuntimeServices();
            m_RuntimeServicesDetached = true;
        }
        catch (...)
        {
            ReportShutdownFailure("detach-runtime-services", std::current_exception());
        }
    }

    void EditorManagedRuntimeCoordinator::Shutdown() noexcept
    {
        if (!m_Lifetime.BeginShutdown())
            return;

        m_ScheduledBuildDelaySeconds = -1.0;
        m_BuildDelayElapsedSeconds = 0.0;
        if (!m_RuntimeServicesDetached)
        {
            try
            {
                m_Dependencies.DetachRuntimeServices();
                m_RuntimeServicesDetached = true;
            }
            catch (...)
            {
                ReportShutdownFailure("detach-runtime-services", std::current_exception());
            }
        }
        try
        {
            m_Dependencies.ResetRuntimeInput();
        }
        catch (...)
        {
            ReportShutdownFailure("reset-runtime-input", std::current_exception());
        }
    }

    EditorWorkspaceCallbackToken EditorManagedRuntimeCoordinator::CaptureCallbackToken() const
    {
        return m_Lifetime.CaptureCallbackToken();
    }

    bool EditorManagedRuntimeCoordinator::ShutdownComplete() const { return m_Lifetime.ShutdownComplete(); }

    void EditorManagedRuntimeCoordinator::ReportShutdownFailure(const std::string_view operation,
                                                                const std::exception_ptr& failure) const noexcept
    {
        if (!m_Dependencies.ReportShutdownFailure)
            return;
        try
        {
            m_Dependencies.ReportShutdownFailure(operation, failure);
        }
        catch (...)
        {
        }
    }
} // namespace KeireEditor
