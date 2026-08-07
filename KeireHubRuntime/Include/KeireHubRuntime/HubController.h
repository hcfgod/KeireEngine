#pragma once

#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubProjectCatalog.h"
#include "KeireHubRuntime/HubSettingsStore.h"
#include "KeireHubRuntime/HubTaskStore.h"
#include "KeireHubRuntime/HubUpdateManager.h"
#include "KeireHubRuntime/NotificationStore.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

namespace KeireHub
{
    struct HubRuntimePaths final
    {
        std::filesystem::path PreferenceRoot;
        std::filesystem::path LegacySettingsPath;
    };

    struct HubControllerSnapshot final
    {
        std::shared_ptr<const HubSettings> Settings;
        std::shared_ptr<const std::vector<HubRecentProject>> Projects;
        std::shared_ptr<const std::vector<EditorInstallation>> Installations;
        std::shared_ptr<const std::vector<HubTask>> Tasks;
        std::shared_ptr<const std::vector<HubNotification>> Notifications;
        std::size_t UnreadNotifications = 0;
    };

    class HubController final
    {
      public:
        using WorkerProbe = std::function<bool(std::uint64_t)>;

        explicit HubController(HubRuntimePaths paths);

        [[nodiscard]] HubStatus Load(std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus ReconcileInterruptedTasks(std::uint64_t nowUnixSeconds, WorkerProbe workerProbe = {});
        // Both batches are preflighted before persistence. A second-registry failure restores the first registry's
        // exact prior snapshot and durable contents before returning.
        [[nodiscard]] HubStatus UpsertProjectsAndInstallations(std::span<const HubRecentProject> projects,
                                                               std::span<const EditorInstallation> installations);
        [[nodiscard]] HubControllerSnapshot Snapshot() const noexcept;

        [[nodiscard]] HubSettingsStore& Settings() noexcept;
        [[nodiscard]] HubProjectCatalog& Projects() noexcept;
        [[nodiscard]] EditorInstallationRegistry& Installations() noexcept;
        [[nodiscard]] HubTaskStore& Tasks() noexcept;
        [[nodiscard]] NotificationStore& Notifications() noexcept;
        [[nodiscard]] HubUpdateManager& Updates() noexcept;

      private:
        HubSettingsStore m_Settings;
        HubProjectCatalog m_Projects;
        EditorInstallationRegistry m_Installations;
        HubTaskStore m_Tasks;
        NotificationStore m_Notifications;
        HubUpdateManager m_Updates;
    };
} // namespace KeireHub
