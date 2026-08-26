#pragma once

#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <exception>
#include <functional>
#include <string_view>

namespace KeireEditor
{
    struct EditorBuildCookDependencies final
    {
        std::function<void()> UpdateBuild;
        std::function<bool()> AssetDatabaseReady;
        std::function<void()> ShutdownBuild;
        std::function<void(std::string_view, const std::exception_ptr&)> ReportShutdownFailure;
    };

    class EditorBuildCookCoordinator final
    {
      public:
        explicit EditorBuildCookCoordinator(EditorBuildCookDependencies dependencies);
        ~EditorBuildCookCoordinator() noexcept;

        EditorBuildCookCoordinator(const EditorBuildCookCoordinator&) = delete;
        EditorBuildCookCoordinator& operator=(const EditorBuildCookCoordinator&) = delete;
        EditorBuildCookCoordinator(EditorBuildCookCoordinator&&) = delete;
        EditorBuildCookCoordinator& operator=(EditorBuildCookCoordinator&&) = delete;

        [[nodiscard]] bool Update();
        void Shutdown() noexcept;

        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] bool ShutdownComplete() const;

      private:
        EditorBuildCookDependencies m_Dependencies;
        EditorCoordinatorLifetime m_Lifetime{"build-cook coordinator"};
    };
} // namespace KeireEditor
