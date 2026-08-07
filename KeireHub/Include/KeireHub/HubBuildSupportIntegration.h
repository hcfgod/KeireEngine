#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/BuildSupportPlanning.h"

#include "Keire/Ui.h"
#include "Keire/Window.h"

#include <memory>
#include <string_view>

namespace KeireHub
{
    class HubBuildSupportIntegration final
    {
      public:
        HubBuildSupportIntegration();
        ~HubBuildSupportIntegration();

        HubBuildSupportIntegration(const HubBuildSupportIntegration&) = delete;
        HubBuildSupportIntegration& operator=(const HubBuildSupportIntegration&) = delete;
        HubBuildSupportIntegration(HubBuildSupportIntegration&&) = delete;
        HubBuildSupportIntegration& operator=(HubBuildSupportIntegration&&) = delete;

        [[nodiscard]] HubStatus Refresh();
        void SynchronizeEditors(HubProductSnapshot& product);
        [[nodiscard]] HubStatus ManageEditor(std::string_view installationId);
        [[nodiscard]] HubStatus FocusTarget(std::string_view platform, std::string_view architecture);
        [[nodiscard]] HubStatus ImportPackage(const std::filesystem::path& package);
        [[nodiscard]] bool OwnsTask(std::string_view taskId) const noexcept;
        [[nodiscard]] HubStatus ExecuteTaskCommand(const HubUiCommand& command);
        void Poll();
        void Draw(Keire::UiFrame& ui, Keire::WindowSystem& windows, Keire::WindowId window, bool offlineMode = false);
        void Stop() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireHub
