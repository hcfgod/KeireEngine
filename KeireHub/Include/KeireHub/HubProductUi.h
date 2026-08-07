#pragma once

#include "KeireHub/HubDesignTokens.h"
#include "KeireHub/HubTemplateBrowser.h"

#include "Keire/Ui.h"
#include "Keire/Window.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    struct HubEditorUiRecord final
    {
        std::string Id;
        std::string Version;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
        std::filesystem::path Root;
        std::filesystem::path Entrypoint;
        std::filesystem::path AssetToolEntrypoint;
        std::string BundledDotnetSdk;
        std::uint32_t MinimumProjectSchema = 1;
        std::uint32_t MaximumProjectSchema = 1;
        std::uint64_t InstalledBytes = 0;
        std::size_t ProjectCount = 0;
        std::size_t ComponentCount = 0;
        bool Managed = false;
        bool Healthy = false;
        bool RepairAvailable = false;
        std::string HealthLabel = "Unknown";
        std::vector<std::string> HealthIssues;
        bool Running = false;
        bool HasActiveTask = false;
        bool ManagementBusy = false;
        std::string ManagementStatus;
    };

    struct HubEditorComponentSelectionUiRecord final
    {
        std::string PackageId;
        std::string Version;

        [[nodiscard]] bool operator==(const HubEditorComponentSelectionUiRecord&) const noexcept = default;
    };

    struct HubAvailableEditorComponentUiRecord final
    {
        std::string PackageId;
        std::string Version;
        std::string DisplayName;
        std::uint64_t DownloadBytes = 0;
        std::uint64_t InstalledBytes = 0;
        bool Required = false;
        std::vector<std::string> RequiredBy;
    };

    struct HubAvailableEditorUiRecord final
    {
        std::string PackageId;
        std::string Version;
        std::string DisplayName;
        std::string Channel;
        std::string Platform;
        std::string Architecture;
        std::uint64_t DownloadBytes = 0;
        std::uint64_t InstalledBytes = 0;
        std::vector<std::string> InstalledInstallationIds;
        std::vector<HubAvailableEditorComponentUiRecord> Components;
        std::string AvailabilityMessage;
    };

    struct HubEditorInstallUiRequest final
    {
        std::filesystem::path Destination;
        std::string EditorPackageId;
        std::string EditorVersion;
        std::vector<HubEditorComponentSelectionUiRecord> Components;

        [[nodiscard]] bool operator==(const HubEditorInstallUiRequest&) const noexcept = default;
    };

    struct HubEditorInstallStepUiRecord final
    {
        std::string PackageId;
        std::string DisplayName;
        std::string Version;
        std::uint64_t DownloadBytes = 0;
        bool ExplicitlySelected = false;
        std::vector<std::string> RequiredBy;
    };

    struct HubEditorInstallPreviewUiRecord final
    {
        HubEditorInstallUiRequest Request;
        std::string InstallationId;
        std::string Channel;
        std::uint64_t DownloadBytes = 0;
        std::uint64_t RequiredDiskBytes = 0;
        std::vector<HubEditorInstallStepUiRecord> Steps;
    };

    struct HubContentUiRecord final
    {
        std::string Id;
        std::string Title;
        std::string Summary;
        std::string Category;
        std::string Difficulty;
        std::filesystem::path LocalPath;
        std::string Url;
        bool Featured = false;
        bool Remote = false;
    };

    struct HubLicenseUiRecord final
    {
        std::string Id;
        std::string Name;
        std::string Group;
        std::filesystem::path Path;
        std::string Text;
    };

    struct HubTaskUiRecord final
    {
        std::string Id;
        std::string Title;
        std::string Phase;
        std::string Message;
        std::string CurrentPackage;
        float Progress = 0.0F;
        std::uint64_t BytesTransferred = 0;
        std::uint64_t TotalBytes = 0;
        std::uint64_t BytesPerSecond = 0;
        std::uint32_t RemainingComponents = 0;
        bool Active = false;
        bool Pausable = false;
        bool Paused = false;
        bool Cancellable = false;
        bool Retryable = false;
    };

    struct HubNotificationUiRecord final
    {
        std::string Id;
        std::string Severity;
        std::string Title;
        std::string Message;
        bool Read = false;
    };

    struct HubUpdateUiRecord final
    {
        std::string PackageId;
        std::string Version;
        std::string Channel;
        std::string TaskId;
        std::string ActionMessage;
        std::filesystem::path VerifiedInstallerPath;
        std::uint64_t DownloadBytes = 0;
        bool Required = false;
        bool DownloadActive = false;
        bool DownloadPaused = false;
        bool DownloadFailed = false;
        bool CanDownload = false;
        bool ReadyToInstall = false;
        bool NativeHandoffAvailable = false;
    };

    struct HubFirstRunItemUiRecord final
    {
        std::string Name;
        std::filesystem::path Root;
        std::string Detail;
    };

    struct HubProductSnapshot final
    {
        std::string HubVersion;
        std::size_t RecentProjects = 0;
        std::size_t PinnedProjects = 0;
        std::size_t HealthyComponents = 0;
        std::size_t UnhealthyComponents = 0;
        bool BuildSupportBusy = false;
        bool BuildSupportInventoryLoading = false;
        bool Online = false;
        bool CatalogAvailable = false;
        std::vector<HubEditorUiRecord> Editors;
        std::vector<std::string> PopulatedEditorChannels;
        std::shared_ptr<const std::vector<HubAvailableEditorUiRecord>> AvailableEditors;
        std::shared_ptr<const HubEditorInstallPreviewUiRecord> EditorInstallPreview;
        std::string EditorCatalogMessage;
        std::string EditorInstallPreviewMessage;
        bool EditorCatalogRefreshing = false;
        bool EditorManagementBusy = false;
        bool EditorManagementRefreshing = false;
        std::vector<HubTemplateUiRecord> Templates;
        std::vector<HubContentUiRecord> Learn;
        std::vector<HubContentUiRecord> Resources;
        std::vector<HubLicenseUiRecord> Licenses;
        std::string ContentMessage;
        std::string LocalLicenseMessage;
        std::string LicenseMessage;
        std::vector<HubTaskUiRecord> Tasks;
        std::vector<HubNotificationUiRecord> Notifications;
        std::size_t UnreadNotifications = 0;
        std::optional<HubUpdateUiRecord> HubUpdate;
        std::string HubUpdateMessage;
        bool FirstRunDiscoveryRunning = false;
        bool FirstRunDiscoveryComplete = false;
        std::size_t DiscoveredProjects = 0;
        std::size_t DiscoveredEditors = 0;
        std::vector<HubFirstRunItemUiRecord> DiscoveredProjectItems;
        std::vector<HubFirstRunItemUiRecord> DiscoveredEditorItems;
        std::string FirstRunDiscoveryMessage;
        bool ProjectCreationBusy = false;
        float ProjectCreationProgress = 0.0F;
        std::string ProjectCreationEditorId;
        std::string ProjectCreationMessage;
        bool VerifiedCacheClearRunning = false;
        HubSettings Settings;
    };

    enum class HubUiCommandType
    {
        None,
        CreateProjectFromTemplate,
        AddProject,
        RefreshProjects,
        LocateEditor,
        PreviewEditorInstall,
        InstallEditor,
        ManageBuildSupport,
        VerifyEditor,
        RepairManagedEditor,
        RemoveExternalEditor,
        RemoveManagedEditor,
        RevealPath,
        OpenUrl,
        OpenLocalContent,
        CopyText,
        BeginFirstRunDiscovery,
        SaveSettings,
        ResetSettings,
        ClearVerifiedCache,
        OpenLogs,
        CopyDiagnostics,
        PauseTask,
        ResumeTask,
        CancelTask,
        RetryTask,
        DownloadHubUpdate,
        InstallHubUpdate,
        MarkNotificationRead,
        ClearNotifications
    };

    struct HubUiCommand final
    {
        HubUiCommandType Type = HubUiCommandType::None;
        std::string ItemId;
        std::filesystem::path Path;
        std::string Url;
        std::string Text;
        std::optional<HubSettings> Settings;
        std::optional<HubEditorInstallUiRequest> EditorInstall;

        [[nodiscard]] explicit operator bool() const noexcept { return Type != HubUiCommandType::None; }
    };

    enum class HubFatalUiAction
    {
        None,
        OpenLogs,
        CopyDiagnostics,
        Close
    };

    struct HubFatalUiState final
    {
        std::string Message;
        std::string ActionMessage;
        bool LogsAvailable = false;
    };

    class HubProductUi final
    {
      public:
        HubProductUi() = default;

        void SetAppearance(HubAppearance appearance, bool systemPrefersDark = true) noexcept;
        void ResetSettingsEditor() noexcept { m_EditedSettings.reset(); }
        [[nodiscard]] bool RequestEditorInstall(std::string_view packageOrVersion, const HubProductSnapshot& snapshot);

        void DrawTitleBar(Keire::UiFrame& ui, Keire::Window& window, HubPage page, const HubProductSnapshot& snapshot,
                          HubUiCommand& command);
        void DrawFatalTitleBar(Keire::UiFrame& ui, Keire::Window& window);
        [[nodiscard]] HubFatalUiAction DrawFatalScreen(Keire::UiFrame& ui, const HubFatalUiState& state);
        [[nodiscard]] HubFatalUiAction DrawFatalRecoveryWindow(Keire::UiFrame& ui, Keire::Window& window,
                                                               const HubFatalUiState& state);
        void DrawSidebar(Keire::UiFrame& ui, HubPage& page, bool compact, const HubProductSnapshot& snapshot);
        void DrawHome(Keire::UiFrame& ui, HubPage& page, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawInstalls(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawTemplates(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawLearn(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawResources(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawLicenses(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawSettings(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawTaskCenter(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawNotificationCenter(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        void DrawFirstRun(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);

        [[nodiscard]] bool TaskCenterOpen() const noexcept { return m_TaskCenterOpen; }
        void SetTaskCenterOpen(bool open) noexcept { m_TaskCenterOpen = open; }

      private:
        struct TemplateArtworkTexture final
        {
            std::filesystem::path Path;
            std::shared_ptr<const std::vector<std::byte>> Pixels;
            Keire::Ref<Keire::UiImage> Image;
        };

        void SynchronizeSettings(const HubSettings& settings);
        void DrawAvailableEditors(Keire::UiFrame& ui, const HubProductSnapshot& snapshot);
        void DrawEditorInstallDialog(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command);
        [[nodiscard]] Keire::Ref<Keire::UiImage> ResolveTemplateArtwork(Keire::UiFrame& ui,
                                                                        const HubTemplateArtworkUiRecord& artwork);

        std::optional<HubSettings> m_EditedSettings;
        std::string m_LicenseSearch;
        std::string m_ContentSearch;
        std::string m_TemplateSearch;
        std::string m_SelectedTemplateId;
        HubTemplateCategoryFilter m_TemplateCategory = HubTemplateCategoryFilter::All;
        std::optional<HubEditorUiRecord> m_PendingEditorRemoval;
        std::optional<HubAvailableEditorUiRecord> m_PendingEditorInstall;
        std::optional<HubEditorInstallUiRequest> m_LastEditorInstallRequest;
        std::string m_EditorInstallDestination;
        std::string m_EditorComponentSearch;
        std::vector<HubEditorComponentSelectionUiRecord> m_SelectedEditorComponents;
        std::vector<TemplateArtworkTexture> m_TemplateArtworkTextures;
        bool m_TaskCenterOpen = false;
        bool m_NotificationCenterOpen = false;
        bool m_RequestEditorRemoval = false;
        bool m_ConfirmManagedEditorRemoval = false;
        bool m_RequestEditorInstall = false;
        bool m_RequestResetSettings = false;
        bool m_RequestClearCache = false;
        std::size_t m_FirstRunStep = 0;
        HubDesignTokens m_Tokens = HubDesignTokens::For(HubAppearance::Dark);
    };
} // namespace KeireHub
