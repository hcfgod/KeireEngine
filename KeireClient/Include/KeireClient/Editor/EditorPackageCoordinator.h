#pragma once

#include "Keire/Core.h"
#include "KeireClient/Editor/AssetPackageAuthoring.h"
#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace KeireEditor
{
    struct EditorPackageCoordinatorDependencies final
    {
        std::function<void()> ShutdownPanel;
        std::function<Keire::Ref<Keire::AssetDatabase>()> AssetDatabase;
        std::function<std::span<const Keire::AssetSourceRecord>()> AssetRecords;
        std::function<Keire::Ref<Keire::WindowSystem>()> Windows;
        std::function<Keire::WindowId()> MainWindow;
        std::function<void(std::string)> SetStatus;
        std::function<void(std::string)> SetError;
        std::function<void(std::string_view, const std::exception_ptr&)> ReportShutdownFailure;
    };

    class EditorPackageCoordinator final
    {
      public:
        explicit EditorPackageCoordinator(EditorPackageCoordinatorDependencies dependencies);
        ~EditorPackageCoordinator() noexcept;

        EditorPackageCoordinator(const EditorPackageCoordinator&) = delete;
        EditorPackageCoordinator& operator=(const EditorPackageCoordinator&) = delete;
        EditorPackageCoordinator(EditorPackageCoordinator&&) = delete;
        EditorPackageCoordinator& operator=(EditorPackageCoordinator&&) = delete;

        void CreateAssetPackage(AssetPackageSelection selection, AssetPackageDraft draft);
        void Update();
        void ShutdownPanel() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool Busy() const;
        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] bool ShutdownComplete() const;

      private:
        struct PendingDialog final
        {
            AssetPackageSelection Selection;
            AssetPackageDraft Draft;
            Keire::Ref<Keire::SaveFileDialogOperation> Dialog;
        };

        void CompleteExport();
        void CompleteDialog();
        void ReportShutdownFailure(std::string_view operation, const std::exception_ptr& failure) noexcept;

        EditorCoordinatorLifetime m_Lifetime{"Editor package coordinator"};
        EditorPackageCoordinatorDependencies m_Dependencies;
        std::optional<PendingDialog> m_PendingDialog;
        std::future<Keire::AssetPackageArchiveMetadata> m_Export;
        std::filesystem::path m_Output;
        bool m_PanelShutdown = false;
    };
} // namespace KeireEditor
