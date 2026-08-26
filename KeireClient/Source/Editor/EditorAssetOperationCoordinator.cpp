#include "KeireClient/Editor/EditorAssetOperationCoordinator.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    EditorAssetOperationCoordinator::EditorAssetOperationCoordinator(EditorAssetOperationDependencies dependencies)
        : m_Dependencies(std::move(dependencies))
    {
        if (!m_Dependencies.UpdateOperations || !m_Dependencies.DrainQueuedMutation ||
            !m_Dependencies.DrainQueuedPrefab || !m_Dependencies.BusyOrPending || !m_Dependencies.PollHotReload ||
            !m_Dependencies.ShutdownOperations || !m_Dependencies.CloseAssetWorkspace)
        {
            throw std::invalid_argument("Asset-operation coordinator dependencies must be callable.");
        }
    }

    EditorAssetOperationCoordinator::~EditorAssetOperationCoordinator() noexcept { Shutdown(); }

    void EditorAssetOperationCoordinator::UpdateOperations()
    {
        m_Lifetime.RequireOwnerThread("UpdateOperations");
        m_Dependencies.UpdateOperations();
    }

    void EditorAssetOperationCoordinator::DrainQueuedMutation()
    {
        m_Lifetime.RequireOwnerThread("DrainQueuedMutation");
        m_Dependencies.DrainQueuedMutation();
    }

    void EditorAssetOperationCoordinator::DrainQueuedPrefab()
    {
        m_Lifetime.RequireOwnerThread("DrainQueuedPrefab");
        m_Dependencies.DrainQueuedPrefab();
    }

    bool EditorAssetOperationCoordinator::AdmitPolling(const double unscaledDeltaSeconds)
    {
        m_Lifetime.RequireOwnerThread("AdmitPolling");
        if (!std::isfinite(unscaledDeltaSeconds) || unscaledDeltaSeconds < 0.0)
            throw std::invalid_argument("Asset polling delta must be finite and non-negative.");
        if (m_Dependencies.BusyOrPending())
            return false;
        m_PollSeconds += unscaledDeltaSeconds;
        if (m_PollSeconds < 0.1)
            return false;
        m_PollSeconds = 0.0;
        return true;
    }

    void EditorAssetOperationCoordinator::PollHotReload()
    {
        m_Lifetime.RequireOwnerThread("PollHotReload");
        m_Dependencies.PollHotReload();
    }

    void EditorAssetOperationCoordinator::ShutdownOperations() noexcept
    {
        if (m_OperationsShutDown)
            return;
        try
        {
            m_Lifetime.RequireOwnerThread("ShutdownOperations");
            m_Dependencies.ShutdownOperations();
            m_OperationsShutDown = true;
        }
        catch (...)
        {
            ReportShutdownFailure("shutdown-operations", std::current_exception());
        }
    }

    void EditorAssetOperationCoordinator::Shutdown() noexcept
    {
        if (!m_Lifetime.BeginShutdown())
            return;
        if (!m_OperationsShutDown)
        {
            try
            {
                m_Dependencies.ShutdownOperations();
                m_OperationsShutDown = true;
            }
            catch (...)
            {
                ReportShutdownFailure("shutdown-operations", std::current_exception());
            }
        }
        try
        {
            m_Dependencies.CloseAssetWorkspace();
        }
        catch (...)
        {
            ReportShutdownFailure("close-asset-workspace", std::current_exception());
        }
    }

    EditorWorkspaceCallbackToken EditorAssetOperationCoordinator::CaptureCallbackToken() const
    {
        return m_Lifetime.CaptureCallbackToken();
    }

    bool EditorAssetOperationCoordinator::ShutdownComplete() const { return m_Lifetime.ShutdownComplete(); }

    void EditorAssetOperationCoordinator::ReportShutdownFailure(const std::string_view operation,
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
