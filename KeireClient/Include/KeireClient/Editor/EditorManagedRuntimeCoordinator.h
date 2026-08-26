#pragma once

#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <exception>
#include <functional>
#include <string>
#include <string_view>

namespace KeireEditor
{
    struct EditorManagedRuntimeDependencies final
    {
        std::function<void()> StartBuild;
        std::function<void()> PollBuild;
        std::function<void(std::string)> ReportBuildError;
        std::function<void()> DetachRuntimeServices;
        std::function<void()> ResetRuntimeInput;
        std::function<void(std::string_view, const std::exception_ptr&)> ReportShutdownFailure;
    };

    class EditorManagedRuntimeCoordinator final
    {
      public:
        explicit EditorManagedRuntimeCoordinator(EditorManagedRuntimeDependencies dependencies);
        ~EditorManagedRuntimeCoordinator() noexcept;

        EditorManagedRuntimeCoordinator(const EditorManagedRuntimeCoordinator&) = delete;
        EditorManagedRuntimeCoordinator& operator=(const EditorManagedRuntimeCoordinator&) = delete;
        EditorManagedRuntimeCoordinator(EditorManagedRuntimeCoordinator&&) = delete;
        EditorManagedRuntimeCoordinator& operator=(EditorManagedRuntimeCoordinator&&) = delete;

        void ScheduleBuild(double delaySeconds);
        void Update(double unscaledDeltaSeconds);
        void DetachRuntimeServices() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] bool ShutdownComplete() const;

      private:
        void ReportShutdownFailure(std::string_view operation, const std::exception_ptr& failure) const noexcept;

        EditorManagedRuntimeDependencies m_Dependencies;
        EditorCoordinatorLifetime m_Lifetime{"managed-runtime coordinator"};
        double m_ScheduledBuildDelaySeconds = -1.0;
        double m_BuildDelayElapsedSeconds = 0.0;
        bool m_RuntimeServicesDetached = false;
    };
} // namespace KeireEditor
