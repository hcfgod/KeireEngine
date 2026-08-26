#pragma once

#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <exception>
#include <functional>
#include <string_view>

namespace KeireEditor
{
    struct EditorPlayModeDependencies final
    {
        std::function<void()> ProcessSceneTransition;
        std::function<void()> FinalizeEditorMutation;
        std::function<void()> CompletePendingTransition;
        std::function<void(double, double)> UpdateRuntime;
        std::function<void()> ContinuePendingPlay;
        std::function<void()> CloseRuntime;
        std::function<void(std::string_view, const std::exception_ptr&)> ReportShutdownFailure;
    };

    class EditorPlayModeCoordinator final
    {
      public:
        explicit EditorPlayModeCoordinator(EditorPlayModeDependencies dependencies);
        ~EditorPlayModeCoordinator() noexcept;

        EditorPlayModeCoordinator(const EditorPlayModeCoordinator&) = delete;
        EditorPlayModeCoordinator& operator=(const EditorPlayModeCoordinator&) = delete;
        EditorPlayModeCoordinator(EditorPlayModeCoordinator&&) = delete;
        EditorPlayModeCoordinator& operator=(EditorPlayModeCoordinator&&) = delete;

        void UpdateTransitions();
        void UpdateRuntime(double deltaSeconds, double interpolationAlpha);
        void ContinuePendingPlay();
        void Shutdown() noexcept;

        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] bool ShutdownComplete() const;

      private:
        EditorPlayModeDependencies m_Dependencies;
        EditorCoordinatorLifetime m_Lifetime{"play-mode coordinator"};
    };
} // namespace KeireEditor
