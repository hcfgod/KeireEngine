#pragma once

#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <exception>
#include <functional>
#include <string_view>

namespace KeireEditor
{
    struct EditorAssetOperationDependencies final
    {
        std::function<void()> UpdateOperations;
        std::function<void()> DrainQueuedMutation;
        std::function<void()> DrainQueuedPrefab;
        std::function<bool()> BusyOrPending;
        std::function<void()> PollHotReload;
        std::function<void()> ShutdownOperations;
        std::function<void()> CloseAssetWorkspace;
        std::function<void(std::string_view, const std::exception_ptr&)> ReportShutdownFailure;
    };

    class EditorAssetOperationCoordinator final
    {
      public:
        explicit EditorAssetOperationCoordinator(EditorAssetOperationDependencies dependencies);
        ~EditorAssetOperationCoordinator() noexcept;

        EditorAssetOperationCoordinator(const EditorAssetOperationCoordinator&) = delete;
        EditorAssetOperationCoordinator& operator=(const EditorAssetOperationCoordinator&) = delete;
        EditorAssetOperationCoordinator(EditorAssetOperationCoordinator&&) = delete;
        EditorAssetOperationCoordinator& operator=(EditorAssetOperationCoordinator&&) = delete;

        void UpdateOperations();
        void DrainQueuedMutation();
        void DrainQueuedPrefab();
        [[nodiscard]] bool AdmitPolling(double unscaledDeltaSeconds);
        void PollHotReload();
        void ShutdownOperations() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] bool ShutdownComplete() const;

      private:
        void ReportShutdownFailure(std::string_view operation, const std::exception_ptr& failure) const noexcept;

        EditorAssetOperationDependencies m_Dependencies;
        EditorCoordinatorLifetime m_Lifetime{"asset-operation coordinator"};
        double m_PollSeconds = 0.0;
        bool m_OperationsShutDown = false;
    };
} // namespace KeireEditor
