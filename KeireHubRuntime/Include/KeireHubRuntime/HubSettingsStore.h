#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    enum class HubPage
    {
        Home,
        Projects,
        Installs,
        Templates,
        Learn,
        Resources,
        Licenses,
        Settings
    };

    enum class HubAppearance
    {
        System,
        Dark,
        Light
    };

    enum class ProjectView
    {
        Table,
        Cards
    };

    enum class ProjectSort
    {
        LastOpened,
        Name,
        Created,
        Modified,
        Version,
        Status,
        Size
    };

    enum class ProxyMode
    {
        System,
        Custom
    };

    struct HubSettings final
    {
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        bool FirstRunCompleted = false;
        HubPage StartupPage = HubPage::Home;
        bool KeepRunningAfterEditorLaunch = true;
        bool CloseToTray = false;
        HubAppearance Appearance = HubAppearance::System;
        bool ReducedMotion = false;
        bool CheckForUpdates = true;

        ProjectView ProjectsView = ProjectView::Table;
        ProjectSort ProjectsSort = ProjectSort::LastOpened;
        std::filesystem::path DefaultProjectLocation;
        std::vector<std::filesystem::path> ProjectDiscoveryRoots;
        bool RemoveMissingProjectsAutomatically = false;
        bool ConfirmProjectRemoval = true;

        std::filesystem::path DefaultEditorRoot;
        std::filesystem::path CacheRoot;
        std::filesystem::path TemporaryRoot;
        std::uint32_t ConcurrentDownloads = 2;
        bool RetainVerifiedCache = true;
        bool EnableStableChannel = true;
        bool EnablePreReleaseChannel = false;
        bool EnableNightlyChannel = false;

        bool OfflineMode = false;
        ProxyMode NetworkProxyMode = ProxyMode::System;
        std::string CustomProxyUrl;
        std::uint64_t BandwidthLimitBytesPerSecond = 0;

        std::string LogLevel = "info";
        std::optional<std::string> DevelopmentServiceUrl;
        std::optional<std::string> DevelopmentTrustedKey;
    };

    class HubSettingsStore final
    {
      public:
        explicit HubSettingsStore(std::filesystem::path settingsPath, std::filesystem::path legacySettingsPath = {});

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] HubStatus Save(HubSettings settings);
        [[nodiscard]] std::shared_ptr<const HubSettings> Snapshot() const noexcept;
        [[nodiscard]] bool MigratedLegacySettings() const noexcept;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

      private:
        std::filesystem::path m_Path;
        std::filesystem::path m_LegacyPath;
        std::shared_ptr<const HubSettings> m_Snapshot;
        bool m_MigratedLegacy = false;
    };
} // namespace KeireHub
