#include "Keire/Core.h"

#include "KeireHub/HubAccountIntegration.h"
#include "KeireHub/HubActivationWorkflow.h"
#include "KeireHub/HubBuildSupportIntegration.h"
#include "KeireHub/HubDiagnostics.h"
#include "KeireHub/HubDistributionWorkflow.h"
#include "KeireHub/HubEditorDiscovery.h"
#include "KeireHub/HubEditorInstallWorkflow.h"
#include "KeireHub/HubEditorLaunch.h"
#include "KeireHub/HubEditorManagementIntegration.h"
#include "KeireHub/HubEditorManagementWorkflow.h"
#include "KeireHub/HubFirstRunIntegration.h"
#include "KeireHub/HubInstance.h"
#include "KeireHub/HubLocalContent.h"
#include "KeireHub/HubMaintenanceIntegration.h"
#include "KeireHub/HubModalUi.h"
#include "KeireHub/HubPackageTaskWorkflow.h"
#include "KeireHub/HubPathMigration.h"
#include "KeireHub/HubProductUi.h"
#include "KeireHub/HubProjectCreationUi.h"
#include "KeireHub/HubProjectMetadataWorkflow.h"
#include "KeireHub/HubProjectMutationIntegration.h"
#include "KeireHub/HubProjectUiSupport.h"
#include "KeireHub/HubProjectUpgradeUi.h"
#include "KeireHub/HubProjectWorkflow.h"
#include "KeireHub/HubProjectsUi.h"
#include "KeireHub/HubRuntimeUiBridge.h"
#include "KeireHub/HubSettingsWorkflow.h"
#include "KeireHub/HubStartupWorkflow.h"
#include "KeireHub/HubTemplateWorkflow.h"
#include "KeireHub/HubUpdateHandoffWorkflow.h"
#include "KeireHub/HubUpdateIntegration.h"
#include "KeireHub/HubWorkflowError.h"

#include "KeireHubRuntime/EditorProcessTracker.h"
#include "KeireHubRuntime/HubUpdateManager.h"
#include "KeireHubRuntime/TaskNotificationTracker.h"

#include "KeireHub/HubLayerFactory.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using PendingStartupActivation = KeireHub::HubActivationRequest;
    using KeireHub::RequireWorkflowSuccess;
    using KeireHub::Utf8Path;

    class HubLayer final : public Keire::Layer, private KeireHub::HubActivationCallbacks
    {
      public:
        HubLayer(std::filesystem::path executable, const bool smoke,
                 std::optional<PendingStartupActivation> pendingStartupActivation,
                 std::shared_ptr<KeireHub::HubInstanceCoordinator> instance)
            : Keire::Layer("ProjectHub"), m_Executable(std::move(executable)), m_Smoke(smoke),
              m_PendingStartupActivation(std::move(pendingStartupActivation)), m_Instance(std::move(instance)),
              m_CreateLocation(Keire::Detail::PathToUtf8(std::filesystem::current_path()))
        {
        }

      protected:
        void OnAttach() override
        {
            const auto localContent = KeireHub::PopulateLocalHubContent(m_Executable, m_ProductSnapshot);
            for (const auto& failure : localContent.Failures)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Local catalog load failed [{}]: {}", KeireHub::ToString(failure.Code),
                                   failure.TechnicalDetails);
            }
            if (!m_Smoke)
            {
                m_Templates = std::make_unique<KeireHub::HubTemplateWorkflow>(m_Executable);
                if (const auto status = m_Templates->Load(); status)
                    m_ProductSnapshot.Templates = m_Templates->UiSnapshot();
                else
                    SetError("Templates unavailable: " + status.Error().Message);
                try
                {
                    Keire::SystemTraySpecification tray;
                    tray.Tooltip = "Kéire Project Hub";
                    tray.Icon = m_Executable.parent_path().parent_path() / "Config" / "Branding" / "Keire.png";
                    if (!std::filesystem::is_regular_file(tray.Icon))
                        tray.Icon = std::filesystem::current_path() / "Config" / "Branding" / "Keire.png";
                    tray.Actions = {{"Show Hub", [this] { ShowHub(); }}, {"Quit", [this] { Owner().RequestExit(); }}};
                    m_Tray = Owner().Windows()->CreateSystemTray(std::move(tray));
                    if (!m_Tray->IsAvailable())
                    {
                        KEIRE_CLIENT_WARN("[Project Hub] System tray unavailable: {}", m_Tray->Diagnostic());
                        m_Notice = "System tray unavailable; the Hub will remain recoverable from the taskbar.";
                    }
                }
                catch (const std::exception& error)
                {
                    ReportUnexpected("The system tray is unavailable.", error);
                }
                Listen<Keire::WindowMinimizedEvent>(
                    [this](const auto& event)
                    {
                        if (m_RuntimeStartupFailure || event.Header.Window != Owner().MainWindow()->Id() ||
                            !TrayAvailable() || !m_ProductSnapshot.Settings.CloseToTray)
                            return Keire::EventFlow::Continue;
                        Owner().MainWindow()->SetVisible(false);
                        return Keire::EventFlow::Handled;
                    });
                Listen<Keire::WindowCloseRequestedEvent>(
                    [this](const auto& event)
                    {
                        if (m_RuntimeStartupFailure || event.Header.Window != Owner().MainWindow()->Id() ||
                            !TrayAvailable() || !m_ProductSnapshot.Settings.CloseToTray)
                            return Keire::EventFlow::Continue;
                        HideHub();
                        return Keire::EventFlow::Handled;
                    });
                Listen<Keire::WindowFileDropEvent>(
                    [this](const auto& event)
                    {
                        if (m_RuntimeStartupFailure || event.Header.Window != Owner().MainWindow()->Id() ||
                            !m_ProjectWorkflow)
                            return Keire::EventFlow::Continue;
                        try
                        {
                            if (event.Paths.empty() || event.Paths.size() > 32)
                                throw std::runtime_error("Drop between one and 32 project folders at a time.");
                            for (const auto& path : event.Paths)
                                RequireWorkflowSuccess(m_ProjectWorkflow->Add(path, KeireHub::HubNowUnixSeconds()));
                            KeireHub::ReloadProjectRegistry(m_Registry);
                            m_Page = KeireHub::HubPage::Projects;
                            m_Notice = std::to_string(event.Paths.size()) + " project folder(s) added to the Hub.";
                            m_NoticeError = false;
                        }
                        catch (const std::exception& error)
                        {
                            ReportUnexpected("The dropped project could not be added.", error);
                        }
                        return Keire::EventFlow::Handled;
                    });
            }
            if (!m_Smoke)
            {
                try
                {
                    m_Registry = Keire::CreateRef<Keire::ProjectRegistry>(
                        std::filesystem::path{}, Keire::ProjectRegistryLoadMode::CachedMetadata);
                    const auto preferenceRoot = m_Registry->Path().parent_path();
                    m_Controller = std::make_unique<KeireHub::HubController>(KeireHub::HubRuntimePaths{
                        .PreferenceRoot = preferenceRoot, .LegacySettingsPath = preferenceRoot / "HubUi.settings"});
                    if (const auto status = m_Controller->Load(KeireHub::HubNowUnixSeconds()); !status)
                        throw std::runtime_error(status.Error().Message);
                    if (const auto status = KeireHub::RepairLegacyHubStorageRoots(m_Controller->Settings()); !status)
                        throw std::runtime_error(status.Error().Message + " " + status.Error().TechnicalDetails);
                    if (m_Controller->Installations().Snapshot()->empty())
                    {
                        const auto packagedEditor = KeireHub::RegisterPackagedEditorIfPresent(
                            *m_Controller, m_Executable.parent_path().parent_path());
                        if (!packagedEditor)
                        {
                            KEIRE_CLIENT_WARN("[Project Hub] Packaged editor registration was rejected: {}",
                                              packagedEditor.Error().TechnicalDetails);
                            SetError("The packaged editor could not be registered because its manifest is invalid. "
                                     "Locate a verified editor from Installs.");
                        }
                    }
                    for (const auto& failure : localContent.Failures)
                        SetError(failure.Message);
                    m_ProjectWorkflow = std::make_unique<KeireHub::HubProjectWorkflow>(*m_Controller);
                    m_ProjectMutations = std::make_unique<KeireHub::HubProjectMutationWorkflow>(
                        KeireHub::CreateHubProjectMutationServices(*m_ProjectWorkflow));
                    m_ProjectMetadata = std::make_unique<KeireHub::HubProjectMetadataWorkflow>();
                    RequireWorkflowSuccess(m_ProjectMetadata->Start(*m_Controller));
                    m_FirstRun = std::make_unique<KeireHub::HubFirstRunWorkflow>();
                    m_EditorManagement = std::make_unique<KeireHub::HubEditorManagementWorkflow>(
                        *m_Controller,
                        KeireHub::HubEditorManagementSpecification{
                            .HostPlatform = std::string(Keire::GetBuildInfo().Platform),
                            .HostArchitecture = std::string(Keire::GetBuildInfo().Architecture),
                            .ProbeRunning = [this](const KeireHub::EditorInstallation& installation)
                            { return m_EditorProcesses.IsInstallationRunning(installation.Id); }});
                    RequireWorkflowSuccess(m_EditorManagement->Refresh());
                    m_EditorInstalls = std::make_unique<KeireHub::HubEditorInstallWorkflow>(
                        m_Controller->Installations(), std::string(KeireHub::HubUpdateManager::HostPlatformIdentity()),
                        std::string(KeireHub::HubUpdateManager::HostArchitectureIdentity()));
                    m_BuildSupport = std::make_unique<KeireHub::HubBuildSupportIntegration>();
                    if (const auto status = m_BuildSupport->Refresh(); !status)
                        SetError(status.Error().Message);
                    KeireHub::ApplyRuntimeSnapshot(m_Controller->Snapshot(), m_ProductSnapshot);
                    if (m_ProductSnapshot.Settings.DefaultProjectLocation.empty())
                        m_ProductSnapshot.Settings.DefaultProjectLocation = KeireHub::DefaultHubProjectLocation();
                    if (m_ProductSnapshot.Settings.DefaultEditorRoot.empty())
                        m_ProductSnapshot.Settings.DefaultEditorRoot = preferenceRoot / "Editors";
                    if (m_ProductSnapshot.Settings.CacheRoot.empty())
                        m_ProductSnapshot.Settings.CacheRoot = preferenceRoot / "Cache";
                    if (m_ProductSnapshot.Settings.TemporaryRoot.empty())
                        m_ProductSnapshot.Settings.TemporaryRoot = preferenceRoot / "Temporary";
                    if (const auto status = m_Controller->Settings().Save(m_ProductSnapshot.Settings); !status)
                        throw std::runtime_error(status.Error().Message);
                    m_CreateLocation = Keire::Detail::PathToUtf8(m_ProductSnapshot.Settings.DefaultProjectLocation);
                    auto packageTasks = KeireHub::HubPackageTaskWorkflow::Create(*m_Controller, m_Executable,
                                                                                 m_ProductSnapshot.Settings);
                    if (packageTasks)
                    {
                        m_PackageTasks = std::move(packageTasks).Value();
                        RememberPackageTaskSettings();
                    }
                    else
                        SetError("Package task center unavailable: " + packageTasks.Error().Message);
                    m_Distribution = std::make_unique<KeireHub::HubDistributionWorkflow>();
                    RequireWorkflowSuccess(
                        m_Distribution->Start(m_Executable.parent_path().parent_path() / "Config" / "Distribution.json",
                                              m_ProductSnapshot.Settings, m_Executable));
                    RequireWorkflowSuccess(
                        m_Account.Start(m_Executable.parent_path().parent_path() / "Config" / "Supabase.json",
                                        preferenceRoot / "Account" / "session.dat", m_ProductSnapshot.Settings));
                    if (const auto status = KeireHub::PrepareHubStartupRuntime(
                            *m_Controller, Keire::GetBuildInfo().Version, m_ProductSnapshot.Settings.LogLevel,
                            KeireHub::HubNowUnixSeconds());
                        !status)
                        SetError("Hub update recovery failed: " + status.Error().Message);
                    Owner().SetUiTheme(KeireHub::ResolveHubUiTheme(m_ProductSnapshot.Settings.Appearance));
                    const auto projectView = m_ProductSnapshot.Settings.ProjectsView == KeireHub::ProjectView::Cards
                                                 ? KeireHub::HubProjectsView::Cards
                                                 : KeireHub::HubProjectsView::List;
                    const auto projectSort = static_cast<KeireHub::HubProjectsSort>(
                        static_cast<std::uint8_t>(m_ProductSnapshot.Settings.ProjectsSort));
                    m_ProjectsUi.SetPreferences(projectView, projectSort);
                    if (!m_PendingStartupActivation)
                        m_Page = m_ProductSnapshot.Settings.StartupPage;
                }
                catch (const std::exception& error)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Hub runtime startup failed: {}", error.what());
                    m_RuntimeStartupFailure =
                        "The Hub runtime could not initialize its local stores and services. Restart the Hub. If the "
                        "problem continues, review the Hub logs or copy a redacted diagnostic report.";
                }
            }
        }

        void OnDetach() noexcept override
        {
            m_HubUpdateHandoff.Stop();
            m_Account.Stop();
            if (m_PackageTasks)
                m_PackageTasks->Stop();
            if (m_Distribution)
                m_Distribution->Stop();
            if (m_FirstRun)
                m_FirstRun->Cancel();
            m_ProjectMutations.reset();
            if (m_ProjectMetadata)
                m_ProjectMetadata->Cancel();
            if (m_BuildSupport)
                m_BuildSupport->Stop();
            if (m_Tray)
                m_Tray->Close();
            m_Tray.Reset();
        }

        void OnUpdate(const Keire::Time&) override
        {
            const auto desiredTheme = KeireHub::ResolveHubUiTheme(m_ProductSnapshot.Settings.Appearance);
            if (Owner().CurrentUiTheme() != desiredTheme)
                Owner().SetUiTheme(desiredTheme);
            if (m_RuntimeStartupFailure)
                return;
            if (auto handoff = m_HubUpdateHandoff.TakeCompletion())
            {
                if (handoff->Failure)
                    SetError(handoff->Failure->Message);
                else
                    Owner().RequestExit();
            }
            if (m_EditorProcesses.Refresh() && m_EditorManagement)
                m_EditorManagement->ReloadRegistrations();
            if (m_EditorManagement)
                KeireHub::PollHubEditorManagement(*m_EditorManagement, m_EditorInstalls.get(), m_PackageTasks.get(),
                                                  m_Notice, m_NoticeError);
            if (m_ProjectMutations)
            {
                const auto polled = m_ProjectMutations->Poll();
                const auto mutation = m_ProjectMutations->Snapshot();
                if (!polled && !mutation->IsTerminal())
                    SetError(polled.Error().Message);
                if (mutation->IsTerminal() && mutation->OperationId != m_HandledProjectMutation)
                {
                    m_HandledProjectMutation = mutation->OperationId;
                    if (mutation->Failure)
                    {
                        if (!mutation->Failure->TechnicalDetails.empty())
                        {
                            KEIRE_CLIENT_ERROR("[Project Hub] Project duplication failed [{}]: {}",
                                               KeireHub::ToString(mutation->Failure->Code),
                                               mutation->Failure->TechnicalDetails);
                        }
                        SetError(mutation->Failure->Message);
                    }
                    else if (mutation->Result)
                    {
                        KeireHub::ReloadProjectRegistry(m_Registry);
                        m_Notice = "Duplicated project at " + Utf8Path(mutation->Result->Root) + ".";
                        m_NoticeError = false;
                    }
                    else if (mutation->State == KeireHub::HubProjectMutationState::Cancelled)
                    {
                        m_Notice = "Project duplication cancelled.";
                        m_NoticeError = false;
                    }
                    else
                        SetError("Project duplication ended without a result.");
                }
            }
            KeireHub::PollHubMaintenance(m_Maintenance, m_PackageTaskRefreshPending, m_Notice, m_NoticeError);
            if (m_PackageTaskRefreshPending && m_Controller && !m_Maintenance.Snapshot()->IsRunning())
            {
                if (m_PackageTasks)
                    m_PackageTasks->Stop();
                auto packageTasks =
                    KeireHub::HubPackageTaskWorkflow::Create(*m_Controller, m_Executable, m_ProductSnapshot.Settings);
                m_PackageTaskRefreshPending = false;
                if (packageTasks)
                {
                    m_PackageTasks = std::move(packageTasks).Value();
                    RememberPackageTaskSettings();
                }
                else
                {
                    m_PackageTasks.reset();
                    SetError("Package task center unavailable: " + packageTasks.Error().Message);
                }
            }
            if (m_PackageTasks)
            {
                auto registered = m_PackageTasks->ReconcileCompletedEditorInstalls();
                if (!registered)
                    SetError("Editor task reconciliation failed: " + registered.Error().Message);
                else if (registered.Value())
                {
                    if (m_EditorManagement)
                        m_EditorManagement->ReloadRegistrations();
                    if (m_EditorInstalls)
                        m_EditorInstalls->ReloadRegistrations();
                }
                const auto tasks = m_PackageTasks->Snapshot();
                if (tasks->LastFailure && tasks->Revision != m_HandledPackageTaskFailureRevision)
                {
                    m_HandledPackageTaskFailureRevision = tasks->Revision;
                    SetError(tasks->LastFailure->Message);
                }
            }
            if (m_Templates)
            {
                const auto creation = m_Templates->CreationSnapshot();
                const bool terminal = creation->State == KeireHub::HubTemplateCreationState::Completed ||
                                      creation->State == KeireHub::HubTemplateCreationState::Failed;
                if (terminal && creation->OperationId != m_HandledTemplateCreation)
                {
                    m_HandledTemplateCreation = creation->OperationId;
                    if (creation->Failure)
                    {
                        if (!creation->Failure->TechnicalDetails.empty())
                            KEIRE_CLIENT_ERROR("[Project Hub] Project creation failed [{}]: {}",
                                               KeireHub::ToString(creation->Failure->Code),
                                               creation->Failure->TechnicalDetails);
                        SetError(creation->Failure->Message);
                    }
                    else if (creation->Result)
                    {
                        if (!m_ProjectWorkflow)
                        {
                            SetError("The project was created, but the Hub project catalog is unavailable. Add the "
                                     "project folder manually.");
                        }
                        else if (const auto status =
                                     m_ProjectWorkflow->Add(creation->Result->Root, KeireHub::HubNowUnixSeconds());
                                 !status)
                        {
                            SetError("The project was created, but it could not be added to the Hub: " +
                                     status.Error().Message);
                        }
                        else
                        {
                            KeireHub::ReloadProjectRegistry(m_Registry);
                            m_Notice = "Project created at " + Utf8Path(creation->Result->Root) + ".";
                            m_NoticeError = false;
                            if (m_ActiveCreationOpenAfter)
                                Launch(Keire::Project::InspectMetadata(creation->Result->Root), creation->EditorId,
                                       true);
                        }
                    }
                }
            }
            if (m_ProjectMetadata && m_Controller)
            {
                const auto updated = m_ProjectMetadata->Poll(*m_Controller);
                if (!updated)
                    SetError(updated.Error().Message);
                else if (updated.Value())
                {
                    if (m_ProductSnapshot.Settings.RemoveMissingProjectsAutomatically)
                    {
                        const auto cleanup = m_Controller->Projects().RemoveMissing();
                        if (!cleanup)
                            SetError(cleanup.Error().Message);
                    }
                    KeireHub::ReloadProjectRegistry(m_Registry);
                }
            }
            if (m_SettingsDiscoveryPending && m_FirstRun)
            {
                const auto discovery = m_FirstRun->Snapshot();
                if (discovery->State == KeireHub::HubFirstRunWorkflowState::Completed)
                {
                    m_SettingsDiscoveryPending = false;
                    if (!m_Controller)
                        SetError("Discovery results could not be imported because the Hub runtime is unavailable.");
                    else if (const auto status = KeireHub::ImportHubFirstRunSnapshot(*discovery, *m_Controller);
                             !status)
                    {
                        SetError(status.Error().Message);
                    }
                    else
                    {
                        if (m_EditorManagement)
                            m_EditorManagement->ReloadRegistrations();
                        if (m_EditorInstalls)
                            m_EditorInstalls->ReloadRegistrations();
                        KeireHub::ReloadProjectRegistry(m_Registry);
                        m_Notice = "Discovery imported " + std::to_string(discovery->ProjectsFound) +
                                   " project(s) and " + std::to_string(discovery->EditorsFound) + " editor(s).";
                        m_NoticeError = false;
                    }
                }
                else if (discovery->State == KeireHub::HubFirstRunWorkflowState::Failed ||
                         discovery->State == KeireHub::HubFirstRunWorkflowState::Cancelled)
                {
                    m_SettingsDiscoveryPending = false;
                    if (discovery->State == KeireHub::HubFirstRunWorkflowState::Failed)
                        SetError(discovery->Message);
                }
            }
            if (m_DistributionRefreshPending && m_Distribution && !m_Distribution->Snapshot()->Refreshing)
            {
                const auto status =
                    m_Distribution->Start(m_Executable.parent_path().parent_path() / "Config" / "Distribution.json",
                                          m_ProductSnapshot.Settings, m_Executable);
                m_DistributionRefreshPending = false;
                if (!status)
                    SetError("Distribution settings could not be applied: " + status.Error().Message);
            }
            if (const auto status = m_Account.Tick(m_ProductSnapshot.Settings, KeireHub::HubNowUnixSeconds()); !status)
                SetError("Account settings could not be applied: " + status.Error().Message);
            if (m_Instance)
            {
                if (auto activation = m_Instance->PollActivation())
                    KeireHub::HubActivationWorkflow::Dispatch(std::move(*activation), m_Executable, m_Controller.get(),
                                                              m_ProductSnapshot.Editors, m_Page, m_Notice,
                                                              m_NoticeError, *this);
            }
            if (m_Smoke && ++m_Frames >= 8)
                Owner().RequestExit();
            if (m_FolderDialog)
            {
                const auto status = m_FolderDialog->Status();
                if (status == Keire::FolderDialogStatus::Selected)
                {
                    try
                    {
                        if (m_FolderTarget == FolderTarget::CreateLocation)
                            m_CreateLocation = Keire::Detail::PathToUtf8(m_FolderDialog->SelectedPath());
                        else if (m_FolderTarget == FolderTarget::OpenProject)
                            m_OpenPath = Keire::Detail::PathToUtf8(m_FolderDialog->SelectedPath());
                        else if (m_FolderTarget == FolderTarget::LocateEditor)
                            LocateEditor(m_FolderDialog->SelectedPath());
                        else if (m_FolderTarget == FolderTarget::DuplicateProject)
                            m_ProjectsUi.SetDuplicateParent(m_FolderDialog->SelectedPath());
                        else if (m_FolderTarget == FolderTarget::LocateProject)
                            LocateProject(m_FolderDialog->SelectedPath());
                    }
                    catch (const std::exception& error)
                    {
                        ReportUnexpected("The selected folder could not be processed.", error);
                    }
                    m_FolderDialog.Reset();
                    m_FolderTarget = FolderTarget::None;
                }
                else if (status == Keire::FolderDialogStatus::Failed)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Folder dialog failed: {}", m_FolderDialog->Diagnostic());
                    SetError("The folder picker failed. See Hub logs for details.");
                    if (m_FolderTarget == FolderTarget::LocateProject)
                        m_PendingLocateProjectId.clear();
                    m_FolderDialog.Reset();
                    m_FolderTarget = FolderTarget::None;
                }
                else if (status == Keire::FolderDialogStatus::Cancelled)
                {
                    if (m_FolderTarget == FolderTarget::LocateProject)
                        m_PendingLocateProjectId.clear();
                    m_FolderDialog.Reset();
                    m_FolderTarget = FolderTarget::None;
                }
            }
            if (m_BuildSupport)
                m_BuildSupport->Poll();
            if (m_Controller)
            {
                const auto tasks = m_Controller->Tasks().Snapshot();
                if (const auto status = m_TaskNotifications.Observe(*tasks, m_Controller->Notifications(),
                                                                    KeireHub::HubNowUnixSeconds());
                    !status)
                {
                    KEIRE_CLIENT_WARN("[Project Hub] Task notification could not be persisted: {}",
                                      status.Error().Message);
                }
            }
        }

        void OnUi(Keire::UiFrame& ui) override
        {
            if (m_RuntimeStartupFailure)
            {
                DrawRuntimeStartupFailure(ui);
                return;
            }
            if (m_Controller)
                KeireHub::ApplyRuntimeSnapshot(m_Controller->Snapshot(), m_ProductSnapshot);
            if (m_EditorManagement)
                m_EditorManagement->ApplySnapshot(m_ProductSnapshot);
            if (m_PackageTasks)
                m_PackageTasks->ApplySnapshot(m_ProductSnapshot);
            if (m_ProjectMetadata)
            {
                const auto metadata = m_ProjectMetadata->Snapshot();
                KeireHub::ApplyHubProjectMetadataSnapshot(*metadata, m_ProductSnapshot);
                m_ProjectsUi.SetThumbnails(metadata->Thumbnails);
            }
            if (m_FirstRun)
                KeireHub::ApplyHubFirstRunSnapshot(*m_FirstRun->Snapshot(), m_ProductSnapshot);
            if (m_Distribution)
                KeireHub::ApplyHubDistributionSnapshot(*m_Distribution->Snapshot(), m_ProductSnapshot);
            m_Account.ApplySnapshot(m_ProductSnapshot);
            if (m_Distribution && m_PackageTasks)
                KeireHub::ApplyHubUpdateIntegrationSnapshot(*m_Distribution->Snapshot(), *m_PackageTasks->Snapshot(),
                                                            m_ProductSnapshot, m_HubUpdateHandoff.Snapshot()->State);
            if (m_EditorInstalls && m_Distribution)
            {
                const auto distribution = m_Distribution->Snapshot();
                const auto refreshed = m_EditorInstalls->Refresh(*distribution, m_ProductSnapshot.Settings);
                if (!refreshed && !refreshed.Error().TechnicalDetails.empty())
                    KEIRE_CLIENT_ERROR("[Project Hub] Editor catalog refresh failed: {}",
                                       refreshed.Error().TechnicalDetails);
                m_EditorInstalls->ApplySnapshot(m_ProductSnapshot);
            }
            if (m_Templates)
                m_Templates->ApplyCreationSnapshot(m_ProductSnapshot);
            const bool systemPrefersDark = KeireHub::HubSystemPrefersDark();
            m_ProductUi.SetAppearance(m_ProductSnapshot.Settings.Appearance, systemPrefersDark);
            m_ProjectsUi.SetAppearance(m_ProductSnapshot.Settings.Appearance, systemPrefersDark);
            const auto size = Owner().MainWindow()->LogicalSize();
            KeireHub::UpdateHubChromeLayout(*Owner().MainWindow(), size);
            auto projectEntries = m_Registry ? m_Registry->Entries() : std::vector<Keire::RecentProject>{};
            for (auto& project : projectEntries)
            {
                if (m_EditorProcesses.IsProjectRunning(project.Id.ToString()))
                    project.Status = Keire::ProjectStatus::InUse;
            }
            m_ProductSnapshot.RecentProjects = projectEntries.size();
            m_ProductSnapshot.PinnedProjects = std::ranges::count_if(projectEntries, &Keire::RecentProject::Pinned);
            if (m_BuildSupport)
                m_BuildSupport->SynchronizeEditors(m_ProductSnapshot);
            if (m_EditorManagement)
                m_EditorManagement->ApplyOperationSnapshot(m_ProductSnapshot);
            KeireHub::ApplyHubMaintenanceSnapshot(*m_Maintenance.Snapshot(), m_ProductSnapshot);
            if (m_PendingStartupActivation)
            {
                auto activation = std::exchange(m_PendingStartupActivation, std::nullopt);
                KeireHub::HubActivationWorkflow::Dispatch(std::move(*activation), m_Executable, m_Controller.get(),
                                                          m_ProductSnapshot.Editors, m_Page, m_Notice, m_NoticeError,
                                                          *this);
            }
            if (!m_PendingEditorInstallVersion.empty() && !m_ProductSnapshot.EditorCatalogRefreshing)
            {
                if (m_ProductUi.RequestEditorInstall(m_PendingEditorInstallVersion, m_ProductSnapshot))
                {
                    m_Notice = "Review the verified editor package and compatible components before installation.";
                    m_NoticeError = false;
                }
                else
                {
                    SetError("No verified editor catalog entry is available for " + m_PendingEditorInstallVersion +
                             ". No install task was queued.");
                }
                m_PendingEditorInstallVersion.clear();
            }
            ui.SetNextWindowPosition({0.0F, 0.0F}, false);
            ui.SetNextWindowSize({static_cast<float>(size.Width), static_cast<float>(size.Height)}, false);
            const auto tokens =
                KeireHub::HubDesignTokens::For(m_ProductSnapshot.Settings.Appearance, systemPrefersDark);
            [[maybe_unused]] const auto windowPadding =
                ui.PushStyleVariable(Keire::UiStyleVariable::WindowPadding, Keire::UiSize{});
            [[maybe_unused]] const auto windowRounding =
                ui.PushStyleVariable(Keire::UiStyleVariable::WindowRounding, 0.0F);
            [[maybe_unused]] const auto windowBorder =
                ui.PushStyleVariable(Keire::UiStyleVariable::WindowBorderSize, 0.0F);
            [[maybe_unused]] const auto windowBackground =
                ui.PushStyleColor(Keire::UiStyleColorRole::WindowBackground, tokens.Canvas);
            Keire::UiWindowOptions options;
            options.NoTitleBar = true;
            options.NoResize = true;
            options.NoMove = true;
            options.NoCollapse = true;
            options.NoSavedSettings = true;
            if (auto window = ui.BeginWindow("Kéire Project Hub", nullptr, options); window)
            {
                KeireHub::HubUiCommand command;
                if (auto titleBar = ui.BeginChild("HubTitleBar", {0.0F, 40.0F}, false); titleBar)
                    m_ProductUi.DrawTitleBar(ui, *Owner().MainWindow(), m_Page, m_ProductSnapshot, command);
                const bool compact = size.Width < 1080;
                {
                    [[maybe_unused]] const auto sidebarPadding =
                        ui.PushStyleVariable(Keire::UiStyleVariable::WindowPadding,
                                             compact ? Keire::UiSize{8.0F, 12.0F} : Keire::UiSize{14.0F, 12.0F});
                    [[maybe_unused]] const auto sidebarBackground =
                        ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Surface);
                    if (auto sidebar = ui.BeginChild("HubSidebar", {compact ? 72.0F : 224.0F, 0.0F}, true); sidebar)
                        m_ProductUi.DrawSidebar(ui, m_Page, compact, m_ProductSnapshot);
                }
                ui.SameLine();
                {
                    [[maybe_unused]] const auto workspacePadding =
                        ui.PushStyleVariable(Keire::UiStyleVariable::WindowPadding, Keire::UiSize{20.0F, 16.0F});
                    [[maybe_unused]] const auto childPadding =
                        ui.PushStyleVariable(Keire::UiStyleVariable::FramePadding, Keire::UiSize{10.0F, 7.0F});
                    [[maybe_unused]] const auto workspaceBackground =
                        ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Canvas);
                    if (auto workspace = ui.BeginChild("HubWorkspace", {}, false); workspace)
                    {
                        m_ProductUi.DrawNotificationCenter(ui, m_ProductSnapshot, command);
                        m_ProductUi.DrawTaskCenter(ui, m_ProductSnapshot, command);
                        m_ProductUi.DrawAccountDialog(ui, m_ProductSnapshot, command);
                        DrawNotice(ui, tokens);
                        if (m_Page == KeireHub::HubPage::Home)
                            m_ProductUi.DrawHome(ui, m_Page, m_ProductSnapshot, command);
                        else if (m_Page == KeireHub::HubPage::Installs)
                            m_ProductUi.DrawInstalls(ui, m_ProductSnapshot, command);
                        else if (m_Page == KeireHub::HubPage::Templates)
                            m_ProductUi.DrawTemplates(ui, m_ProductSnapshot, command);
                        else if (m_Page == KeireHub::HubPage::Learn)
                            m_ProductUi.DrawLearn(ui, m_ProductSnapshot, command);
                        else if (m_Page == KeireHub::HubPage::Resources)
                            m_ProductUi.DrawResources(ui, m_ProductSnapshot, command);
                        else if (m_Page == KeireHub::HubPage::Licenses)
                            m_ProductUi.DrawLicenses(ui, m_ProductSnapshot, command);
                        else if (m_Page == KeireHub::HubPage::Settings)
                            m_ProductUi.DrawSettings(ui, m_ProductSnapshot, command);
                        else if (m_Page == KeireHub::HubPage::Projects)
                            ExecuteProjectCommand(m_ProjectsUi.Draw(
                                ui, projectEntries, m_ProductSnapshot.Editors, {}, false,
                                m_ProductSnapshot.Settings.ConfirmProjectRemoval, static_cast<bool>(m_FolderDialog)));
                    }
                }
                if (m_BuildSupport)
                    m_BuildSupport->Draw(ui, *Owner().Windows(), Owner().MainWindow()->Id(), tokens,
                                         m_ProductSnapshot.Settings.OfflineMode);
                m_ProductUi.DrawFirstRun(ui, m_ProductSnapshot, command);
                ExecuteProductCommand(command);
                if (std::exchange(m_RequestCreatePopup, false))
                    ui.OpenPopup("Create Project");
                if (std::exchange(m_RequestOpenPopup, false))
                    ui.OpenPopup("Open Project");
                if (std::exchange(m_RequestUpgradePopup, false))
                    ui.OpenPopup("Project Upgrade");
                DrawCreateDialog(ui);
                DrawOpenDialog(ui);
                const auto upgrade = m_ProjectUpgradeUi.Draw(ui);
                if (upgrade.Failure)
                    SetError(upgrade.Failure->Message);
                else if (upgrade.Action != KeireHub::HubProjectUpgradeAction::None)
                {
                    Refresh();
                    if (upgrade.Action == KeireHub::HubProjectUpgradeAction::Reopen)
                        Open(upgrade.Root);
                }
            }
        }

      private:
        enum class FolderTarget : std::uint8_t
        {
            None,
            CreateLocation,
            OpenProject,
            LocateEditor,
            DuplicateProject,
            LocateProject
        };

        void DrawRuntimeStartupFailure(Keire::UiFrame& ui)
        {
            m_ProductUi.SetAppearance(m_ProductSnapshot.Settings.Appearance, KeireHub::HubSystemPrefersDark());
            const auto size = Owner().MainWindow()->LogicalSize();
            KeireHub::UpdateHubChromeLayout(*Owner().MainWindow(), size, false);
            const auto preferenceRoot = Keire::GetPreferenceDirectory();
            const auto action =
                m_ProductUi.DrawFatalRecoveryWindow(ui, *Owner().MainWindow(),
                                                    {.Message = *m_RuntimeStartupFailure,
                                                     .ActionMessage = m_FatalActionMessage,
                                                     .LogsAvailable = KeireHub::HubLogsAvailable(preferenceRoot)});
            const auto outcome =
                KeireHub::HandleHubFatalRecoveryAction(action, m_ProductSnapshot, *Owner().Windows(), preferenceRoot);
            if (!outcome.TechnicalDetails.empty())
                KEIRE_CLIENT_ERROR("[Project Hub] Fatal recovery action failed: {}", outcome.TechnicalDetails);
            if (!outcome.Message.empty())
                m_FatalActionMessage = outcome.Message;
            if (outcome.CloseRequested)
                Owner().RequestExit();
        }

        void DrawNotice(Keire::UiFrame& ui, const KeireHub::HubDesignTokens& tokens)
        {
            const auto now = std::chrono::steady_clock::now();
            if (m_ObservedNotice != m_Notice)
            {
                m_ObservedNotice = m_Notice;
                m_NoticeStarted = now;
            }
            if (!m_NoticeError && !m_Notice.empty() && now - m_NoticeStarted >= std::chrono::seconds(5))
            {
                m_Notice.clear();
                m_ObservedNotice.clear();
            }
            if (m_Notice.empty())
                return;

            [[maybe_unused]] const auto bannerBackground =
                ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Elevated);
            if (auto banner = ui.BeginChild("HubGlobalNotice", {0.0F, 48.0F}, true); banner)
            {
                Keire::UiTableOptions layout;
                layout.Borders = false;
                layout.Resizable = false;
                layout.RowBackground = false;
                layout.PersistSettings = false;
                if (auto table = ui.BeginTable("HubGlobalNoticeLayout", 2, layout); table)
                {
                    ui.TableSetupColumn("Message", Keire::UiTableColumnSizing::Stretch, 1.0F);
                    ui.TableSetupColumn("Action", Keire::UiTableColumnSizing::Fixed, 76.0F);
                    ui.TableNextRow();
                    (void)ui.TableNextColumn();
                    ui.TextColoredWrapped(m_NoticeError ? tokens.Danger : tokens.Success, m_Notice);
                    (void)ui.TableNextColumn();
                    if (ui.Button("Dismiss##HubNotice", {68.0F, 28.0F}))
                    {
                        m_Notice.clear();
                        m_ObservedNotice.clear();
                    }
                }
            }
            ui.Spacing();
        }

        void SavePreferences()
        {
            if (!m_Controller)
                return;
            const auto status = KeireHub::SaveHubProjectPreferences(
                *m_Controller, m_ProjectsUi.View() == KeireHub::HubProjectsView::Cards,
                static_cast<std::uint8_t>(m_ProjectsUi.Sort()));
            if (!status)
                SetError(status.Error().Message);
        }

        void LocateProject(const std::filesystem::path& selected)
        {
            if (!m_ProjectWorkflow || m_PendingLocateProjectId.empty())
                throw std::logic_error("The moved-project workflow is unavailable.");
            auto projectId = std::move(m_PendingLocateProjectId);
            m_PendingLocateProjectId.clear();
            RequireWorkflowSuccess(m_ProjectWorkflow->LocateMovedProject(projectId, selected));
            KeireHub::ReloadProjectRegistry(m_Registry);
            m_Notice = "Moved project located.";
            m_NoticeError = false;
        }

        void ExecuteProjectCommand(const KeireHub::HubProjectUiCommand& command)
        {
            if (!command)
                return;
            try
            {
                switch (command.Type)
                {
                case KeireHub::HubProjectUiCommandType::NewProject:
                    m_RequestCreatePopup = true;
                    break;
                case KeireHub::HubProjectUiCommandType::AddProject:
                    m_RequestOpenPopup = true;
                    break;
                case KeireHub::HubProjectUiCommandType::Refresh:
                    Refresh();
                    break;
                case KeireHub::HubProjectUiCommandType::PreferencesChanged:
                    SavePreferences();
                    break;
                case KeireHub::HubProjectUiCommandType::Open:
                    Open(command.Path);
                    break;
                case KeireHub::HubProjectUiCommandType::OpenWithEditor:
                    Launch(Keire::Project::InspectMetadata(command.Path), command.EditorId, true);
                    break;
                case KeireHub::HubProjectUiCommandType::FindCompatibleEditor:
                    m_Page = KeireHub::HubPage::Installs;
                    if (!command.EditorVersion.empty())
                        m_PendingEditorInstallVersion = command.EditorVersion;
                    m_Notice =
                        command.EditorVersion.empty()
                            ? "Install or locate a compatible editor to open this project."
                            : "Install or locate editor " + command.EditorVersion + " or a compatible newer version.";
                    m_NoticeError = false;
                    break;
                case KeireHub::HubProjectUiCommandType::Reveal:
                    Reveal(command.Path);
                    break;
                case KeireHub::HubProjectUiCommandType::CopyPath:
                    Owner().Windows()->SetClipboardText(Utf8Path(command.Path));
                    m_Notice = "Project path copied to the clipboard.";
                    m_NoticeError = false;
                    break;
                case KeireHub::HubProjectUiCommandType::BrowseDuplicateLocation:
                    BrowseForFolder(FolderTarget::DuplicateProject, command.Path);
                    break;
                case KeireHub::HubProjectUiCommandType::BrowseLocateProject:
                    m_PendingLocateProjectId = command.ProjectId;
                    BrowseForFolder(FolderTarget::LocateProject, command.Path);
                    if (!m_FolderDialog)
                        m_PendingLocateProjectId.clear();
                    break;
                default:
                    if (!m_ProjectWorkflow)
                        throw std::logic_error("Project workflows are unavailable.");
                    if (command.Type == KeireHub::HubProjectUiCommandType::SetPinned)
                    {
                        RequireWorkflowSuccess(m_ProjectWorkflow->SetPinned(command.ProjectId, command.Pinned));
                        m_Notice = command.Pinned ? "Project pinned." : "Project unpinned.";
                    }
                    else if (command.Type == KeireHub::HubProjectUiCommandType::Rename)
                    {
                        RequireWorkflowSuccess(
                            m_ProjectWorkflow->RenameDisplayName(command.ProjectId, command.DisplayName));
                        m_Notice = "Project display name changed to " + command.DisplayName + ".";
                    }
                    else if (command.Type == KeireHub::HubProjectUiCommandType::RemoveFromHub)
                    {
                        RequireWorkflowSuccess(m_ProjectWorkflow->RemoveFromHub(command.ProjectId));
                        m_Notice = command.DisplayName + " was removed from the Hub. Project files were not deleted.";
                    }
                    else if (command.Type == KeireHub::HubProjectUiCommandType::Duplicate)
                    {
                        if (!m_ProjectMutations)
                            throw std::logic_error("Asynchronous project mutations are unavailable.");
                        auto started =
                            m_ProjectMutations->StartDuplicate(command.ProjectId, command.Path, command.DisplayName);
                        if (!started)
                            RequireWorkflowSuccess(KeireHub::HubStatus::Failure(started.Error()));
                        m_Notice = "Duplicating " + command.DisplayName + " in the background...";
                    }
                    else
                        throw std::logic_error("The project command is unsupported.");
                    KeireHub::ReloadProjectRegistry(m_Registry);
                    m_NoticeError = false;
                    break;
                }
            }
            catch (const std::exception& error)
            {
                ReportUnexpected("The project action could not be completed.", error);
            }
        }

        void ExecuteProductCommand(const KeireHub::HubUiCommand& command)
        {
            if (!command)
                return;
            try
            {
                switch (command.Type)
                {
                case KeireHub::HubUiCommandType::CreateProjectFromTemplate:
                    m_CreateTemplateId = command.ItemId;
                    if (!command.Text.empty())
                        m_CreateEditorId = command.Text;
                    m_RequestCreatePopup = true;
                    break;
                case KeireHub::HubUiCommandType::AddProject:
                    m_RequestOpenPopup = true;
                    break;
                case KeireHub::HubUiCommandType::RefreshProjects:
                    Refresh();
                    break;
                case KeireHub::HubUiCommandType::LocateEditor:
                    BrowseForFolder(FolderTarget::LocateEditor, m_Executable.parent_path().parent_path());
                    break;
                case KeireHub::HubUiCommandType::PreviewEditorInstall:
                case KeireHub::HubUiCommandType::InstallEditor:
                {
                    if (!m_EditorInstalls)
                        throw std::logic_error("Editor installation planning is unavailable.");
                    auto result =
                        KeireHub::ExecuteHubEditorInstallCommand(command, *m_EditorInstalls, m_PackageTasks.get());
                    if (!result)
                        RequireWorkflowSuccess(KeireHub::HubStatus::Failure(result.Error()));
                    m_EditorInstalls->ApplySnapshot(m_ProductSnapshot);
                    m_Notice = std::move(result).Value();
                    m_NoticeError = false;
                    break;
                }
                case KeireHub::HubUiCommandType::ManageBuildSupport:
                    if (!m_BuildSupport)
                        throw std::logic_error("Build Support management is unavailable.");
                    RequireWorkflowSuccess(m_BuildSupport->ManageEditor(command.ItemId));
                    break;
                case KeireHub::HubUiCommandType::RepairManagedEditor:
                case KeireHub::HubUiCommandType::VerifyEditor:
                case KeireHub::HubUiCommandType::RemoveExternalEditor:
                case KeireHub::HubUiCommandType::RemoveManagedEditor:
                    if (!m_EditorManagement)
                        throw std::logic_error("Editor management is unavailable.");
                    RequireWorkflowSuccess(KeireHub::BeginHubEditorManagementCommand(
                        *m_EditorManagement, m_EditorInstalls.get(), m_PackageTasks.get(), command, m_Notice,
                        m_NoticeError));
                    break;
                case KeireHub::HubUiCommandType::RevealPath:
                    Reveal(command.Path);
                    break;
                case KeireHub::HubUiCommandType::OpenUrl:
                    Owner().Windows()->OpenUrl(command.Url);
                    break;
                case KeireHub::HubUiCommandType::OpenLocalContent:
                {
                    std::string diagnostic;
                    if (!Keire::Detail::OpenInExternalEditor(command.Path, {}, command.Path.parent_path(), diagnostic))
                        throw std::runtime_error("Could not open content: " + diagnostic);
                    break;
                }
                case KeireHub::HubUiCommandType::CopyText:
                    Owner().Windows()->SetClipboardText(command.Text);
                    m_Notice = "License text copied to the clipboard.";
                    m_NoticeError = false;
                    break;
                case KeireHub::HubUiCommandType::BeginFirstRunDiscovery:
                    if (!m_FirstRun || !command.Settings)
                        throw std::logic_error("First-run discovery is unavailable.");
                    RequireWorkflowSuccess(m_FirstRun->Start(
                        KeireHub::BuildHubFirstRunDiscoveryRequest(*command.Settings, m_Executable,
                                                                   std::string(Keire::GetBuildInfo().Platform),
                                                                   std::string(Keire::GetBuildInfo().Architecture)),
                        KeireHub::HubNowUnixSeconds()));
                    m_SettingsDiscoveryPending = command.ItemId == "settings-discovery";
                    break;
                case KeireHub::HubUiCommandType::SaveSettings:
                {
                    if (!m_Controller || !command.Settings)
                        throw std::logic_error("Hub settings are unavailable.");
                    const bool packageTaskSettingsChanged =
                        (!m_PackageTasks && !m_Maintenance.Snapshot()->IsRunning()) ||
                        command.Settings->TemporaryRoot != m_PackageTaskTemporaryRoot ||
                        command.Settings->ConcurrentDownloads != m_PackageTaskConcurrentDownloads;
                    if (packageTaskSettingsChanged && !PackageTaskReconfigurationSafe())
                    {
                        throw std::runtime_error(
                            "Finish or cancel active package tasks before changing task storage or concurrency.");
                    }
                    if (command.Settings->FirstRunCompleted && !m_ProductSnapshot.Settings.FirstRunCompleted &&
                        m_FirstRun)
                    {
                        if (command.ItemId == "skip-discovery")
                            m_FirstRun->Cancel();
                        else
                            RequireWorkflowSuccess(
                                KeireHub::ImportHubFirstRunSnapshot(*m_FirstRun->Snapshot(), *m_Controller));
                        if (m_EditorManagement)
                            m_EditorManagement->ReloadRegistrations();
                        KeireHub::ReloadProjectRegistry(m_Registry);
                    }
                    if (const auto status = m_Controller->Settings().Save(*command.Settings); !status)
                        throw std::runtime_error(status.Error().Message);
                    m_ProductSnapshot.Settings = *command.Settings;
                    m_CreateLocation = Keire::Detail::PathToUtf8(command.Settings->DefaultProjectLocation);
                    if (m_Distribution)
                        m_DistributionRefreshPending = true;
                    m_Account.RequestRefresh();
                    m_PackageTaskRefreshPending = packageTaskSettingsChanged;
                    m_ProductUi.ResetSettingsEditor();
                    Owner().SetUiTheme(KeireHub::ResolveHubUiTheme(command.Settings->Appearance));
                    m_Notice = "Settings saved.";
                    m_NoticeError = false;
                    break;
                }
                case KeireHub::HubUiCommandType::CopyDiagnostics:
                    Owner().Windows()->SetClipboardText(KeireHub::BuildHubDiagnosticReport(
                        m_ProductSnapshot, Keire::GetBuildInfo(), Keire::GetPreferenceDirectory()));
                    m_Notice = "Diagnostics copied to the clipboard.";
                    m_NoticeError = false;
                    break;
                case KeireHub::HubUiCommandType::OpenLogs:
                {
                    const auto logs = Keire::GetPreferenceDirectory() / "Hub" / "Logs";
                    std::error_code error;
                    std::filesystem::create_directories(logs, error);
                    if (error)
                        throw std::runtime_error("The Hub log directory could not be opened.");
                    Reveal(logs);
                    break;
                }
                case KeireHub::HubUiCommandType::ResetSettings:
                {
                    if (!m_Controller)
                        throw std::logic_error("Hub settings are unavailable.");
                    if (!PackageTaskReconfigurationSafe())
                        throw std::runtime_error("Finish or cancel active package tasks before resetting settings.");
                    auto settings = KeireHub::ResetHubSettings(*m_Controller);
                    if (!settings)
                        throw std::runtime_error(settings.Error().Message);
                    m_ProductSnapshot.Settings = std::move(settings).Value();
                    m_CreateLocation = Keire::Detail::PathToUtf8(m_ProductSnapshot.Settings.DefaultProjectLocation);
                    if (m_FirstRun)
                        m_FirstRun->Cancel();
                    m_SettingsDiscoveryPending = false;
                    m_PackageTaskRefreshPending = true;
                    m_DistributionRefreshPending = true;
                    m_Account.RequestRefresh();
                    m_ProductUi.ResetSettingsEditor();
                    Owner().SetUiTheme(KeireHub::ResolveHubUiTheme(m_ProductSnapshot.Settings.Appearance));
                    m_Page = KeireHub::HubPage::Home;
                    m_Notice = "Hub settings reset.";
                    m_NoticeError = false;
                    break;
                }
                case KeireHub::HubUiCommandType::ClearVerifiedCache:
                {
                    if (!m_Controller)
                        throw std::logic_error("Hub settings are unavailable.");
                    RequireWorkflowSuccess(KeireHub::BeginHubVerifiedCacheClear(
                        m_Maintenance, *m_Controller, m_PackageTasks, m_PackageTaskRefreshPending));
                    break;
                }
                case KeireHub::HubUiCommandType::DownloadHubUpdate:
                {
                    if (!m_Distribution || !m_PackageTasks)
                        throw std::logic_error("The Hub update task system is unavailable.");
                    auto queued = KeireHub::QueueAvailableHubUpdate(*m_Distribution->Snapshot(),
                                                                    m_ProductSnapshot.HubVersion, *m_PackageTasks);
                    if (!queued)
                        RequireWorkflowSuccess(KeireHub::HubStatus::Failure(queued.Error()));
                    m_Notice = "Hub update download added to the task center.";
                    m_NoticeError = false;
                    break;
                }
                case KeireHub::HubUiCommandType::InstallHubUpdate:
                    if (!m_Distribution || !m_PackageTasks || !m_Controller)
                        throw std::logic_error("The Hub update handoff is unavailable.");
                    RequireWorkflowSuccess(KeireHub::StartAvailableHubUpdateHandoff(
                        *m_Distribution->Snapshot(), m_ProductSnapshot.HubVersion, *m_PackageTasks->Snapshot(),
                        m_Controller->Updates(), m_HubUpdateHandoff, m_Executable, m_ProductSnapshot.Settings,
                        KeireHub::HubNowUnixSeconds()));
                    break;
                case KeireHub::HubUiCommandType::AccountSignIn:
                case KeireHub::HubUiCommandType::AccountSignUp:
                case KeireHub::HubUiCommandType::AccountSignOut:
                case KeireHub::HubUiCommandType::SaveAccountProfile:
                    RequireWorkflowSuccess(m_Account.Execute(command));
                    break;
                case KeireHub::HubUiCommandType::None:
                    break;
                case KeireHub::HubUiCommandType::PauseTask:
                case KeireHub::HubUiCommandType::ResumeTask:
                case KeireHub::HubUiCommandType::CancelTask:
                case KeireHub::HubUiCommandType::RetryTask:
                    if (m_BuildSupport && m_BuildSupport->OwnsTask(command.ItemId))
                    {
                        RequireWorkflowSuccess(m_BuildSupport->ExecuteTaskCommand(command));
                    }
                    else
                    {
                        if (!m_PackageTasks)
                            throw std::logic_error("The package task center is unavailable.");
                        RequireWorkflowSuccess(m_PackageTasks->Execute(command));
                    }
                    break;
                case KeireHub::HubUiCommandType::MarkNotificationRead:
                case KeireHub::HubUiCommandType::ClearNotifications:
                    if (!m_Controller)
                        throw std::logic_error("The Hub runtime is unavailable.");
                    if (const auto status =
                            KeireHub::ExecuteRuntimeUiCommand(*m_Controller, command, KeireHub::HubNowUnixSeconds());
                        !status)
                        throw std::runtime_error(status.Error().Message);
                    break;
                }
            }
            catch (const std::exception& error)
            {
                ReportUnexpected("The Hub action could not be completed.", error);
            }
        }

        [[nodiscard]] bool TrayAvailable() const noexcept { return m_Tray && m_Tray->IsAvailable(); }

        void ReportUnexpected(const std::string_view userMessage, const std::exception& error) noexcept
        {
            KEIRE_CLIENT_ERROR("[Project Hub] {} {}", userMessage, error.what());
            SetError(std::string(userMessage) + " See Hub logs for details.");
        }

        [[nodiscard]] bool PackageTaskReconfigurationSafe() const noexcept
        {
            if (!m_PackageTasks)
                return !m_Maintenance.Snapshot()->IsRunning();
            const auto snapshot = m_PackageTasks->Snapshot();
            return snapshot->State == KeireHub::HubWorkerCoordinatorState::Ready && snapshot->Tasks &&
                   std::ranges::none_of(*snapshot->Tasks, [](const KeireHub::HubTask& task)
                                        { return !KeireHub::IsTerminal(task.State); });
        }

        void RememberPackageTaskSettings()
        {
            m_PackageTaskTemporaryRoot = m_ProductSnapshot.Settings.TemporaryRoot;
            m_PackageTaskConcurrentDownloads = m_ProductSnapshot.Settings.ConcurrentDownloads;
        }

        void SetError(std::string message) noexcept
        {
            try
            {
                m_Notice = message;
                m_NoticeError = true;
            }
            catch (...)
            {
            }
            if (m_Controller && message != m_LastPersistedError)
            {
                try
                {
                    const auto status =
                        m_Controller->Notifications().Add({.Id = "hub-error-" + Keire::AssetId::Generate().ToString(),
                                                           .Severity = KeireHub::NotificationSeverity::Error,
                                                           .Title = "Hub action needs attention",
                                                           .Message = message,
                                                           .CreatedUnixSeconds = KeireHub::HubNowUnixSeconds()});
                    if (status)
                        m_LastPersistedError = message;
                    else
                        KEIRE_CLIENT_WARN("[Project Hub] Error notification could not be persisted: {}",
                                          status.Error().Message);
                }
                catch (...)
                {
                }
            }
            try
            {
                KEIRE_CLIENT_ERROR("[Project Hub] {}", message);
            }
            catch (...)
            {
                std::fprintf(stderr, "[Project Hub] %s\n", message.c_str());
            }
        }

        void HideHub()
        {
            const auto window = Owner().MainWindow();
            if (TrayAvailable())
            {
                // Keep the cached minimized state so the hidden Hub uses the bounded background pump rate.
                window->Minimize();
                window->SetVisible(false);
            }
            else
            {
                window->Minimize();
            }
        }

        void ShowHub() override
        {
            if (m_Registry)
            {
                try
                {
                    KeireHub::ReloadProjectRegistry(m_Registry);
                }
                catch (const std::exception& error)
                {
                    ReportUnexpected("The Hub could not refresh its project state.", error);
                }
            }
            const auto window = Owner().MainWindow();
            window->SetVisible(true);
            window->Restore();
            window->Raise();
        }

        void RequestEditorInstall(const std::string_view packageOrVersion) override
        {
            m_PendingEditorInstallVersion = packageOrVersion;
        }

        [[nodiscard]] KeireHub::HubStatus FocusBuildSupport(const std::string_view platform,
                                                            const std::string_view architecture) override
        {
            if (!m_BuildSupport)
            {
                return KeireHub::HubStatus::Failure(
                    {.Code = KeireHub::HubErrorCode::InvalidTransition,
                     .Message = "Build Support management is unavailable until the Hub runtime starts.",
                     .AffectedItem = "build-support"});
            }
            return m_BuildSupport->FocusTarget(platform, architecture);
        }

        void Refresh()
        {
            try
            {
                KeireHub::ReloadProjectRegistry(m_Registry);
                if (m_ProjectMetadata && m_Controller)
                    RequireWorkflowSuccess(m_ProjectMetadata->Start(*m_Controller));
                m_Notice = "Project metadata is refreshing in the background.";
                m_NoticeError = false;
            }
            catch (const std::exception& error)
            {
                ReportUnexpected("Project metadata refresh could not start.", error);
            }
        }

        void LocateEditor(const std::filesystem::path& selected)
        {
            if (!m_Controller)
                throw std::logic_error("The Hub runtime is unavailable.");
            auto inspected = KeireHub::RegisterExternalEditor(*m_Controller, selected,
                                                              "external-" + Keire::ProjectId::Generate().ToString());
            if (!inspected)
                throw std::runtime_error(inspected.Error().Message);
            const auto editor = std::move(inspected).Value();
            if (m_EditorManagement)
                m_EditorManagement->ReloadRegistrations();
            m_Notice = "Located editor " + editor.Version + ". Verify it before use.";
            m_NoticeError = false;
        }

        void StartBuildSupportInstall(const std::filesystem::path& package) override
        {
            if (!m_BuildSupport)
                throw std::logic_error("Build Support management is unavailable.");
            RequireWorkflowSuccess(m_BuildSupport->ImportPackage(package));
        }

        void Launch(const Keire::ProjectInspectionResult& inspection, std::string preferredEditorId = {},
                    const bool requirePreferred = false)
        {
            try
            {
                if (preferredEditorId.empty() && m_Registry)
                {
                    const auto entries = m_Registry->Entries();
                    const auto recent = std::ranges::find(entries, inspection.Id, &Keire::RecentProject::Id);
                    if (recent != entries.end())
                        preferredEditorId = recent->PreferredEditorInstallation;
                }
                auto launched =
                    KeireHub::LaunchProjectEditor(m_ProductSnapshot.Editors, m_EditorProcesses, inspection,
                                                  preferredEditorId, requirePreferred, KeireHub::HubNowUnixSeconds());
                if (!launched)
                {
                    if (!launched.Error().TechnicalDetails.empty())
                        KEIRE_CLIENT_ERROR("[Project Hub] Editor launch failed: {}", launched.Error().TechnicalDetails);
                    throw std::runtime_error(launched.Error().Message);
                }
                const auto& result = launched.Value();
                if (result.TrackingFailure)
                    KEIRE_CLIENT_WARN("[Project Hub] Editor process {} launched but could not be tracked: {}",
                                      result.ProcessId, result.TrackingFailure->Message);
                if (m_EditorManagement)
                    m_EditorManagement->ReloadRegistrations();
                m_Notice = "Opened " + result.Descriptor.Name + ".";
                m_NoticeError = false;
                if (m_ProjectWorkflow)
                {
                    try
                    {
                        const auto status = m_ProjectWorkflow->RecordOpened(
                            inspection.Root, result.Descriptor, result.InstallationId, KeireHub::HubNowUnixSeconds());
                        if (!status)
                            throw std::runtime_error(status.Error().Message);
                        KeireHub::ReloadProjectRegistry(m_Registry);
                    }
                    catch (const std::exception& error)
                    {
                        KEIRE_CLIENT_WARN("[Project Hub] Editor launched, but the recent-project registry could not "
                                          "be updated: {}",
                                          error.what());
                    }
                }
                if (m_ProductSnapshot.Settings.KeepRunningAfterEditorLaunch)
                    HideHub();
                else
                    Owner().RequestExit();
            }
            catch (const std::exception& error)
            {
                ReportUnexpected("The editor could not be launched.", error);
            }
        }

        void BrowseForFolder(const FolderTarget target, const std::filesystem::path& initialPath)
        {
            try
            {
                m_FolderDialog = Owner().Windows()->ShowFolderDialog(Owner().MainWindow()->Id(), initialPath);
                m_FolderTarget = target;
            }
            catch (const std::exception& error)
            {
                ReportUnexpected("The folder picker could not be opened.", error);
            }
        }

        void Reveal(const std::filesystem::path& path)
        {
            std::string diagnostic;
            if (!Keire::Detail::RevealInFileManager(path, diagnostic))
            {
                KEIRE_CLIENT_ERROR("[Project Hub] File reveal failed: {}", diagnostic);
                SetError("The selected item could not be revealed. See Hub logs for details.");
            }
        }

        void Open(const std::filesystem::path& path) override
        {
            try
            {
                const auto inspection = Keire::Project::InspectMetadata(path);
                if (!m_ProjectWorkflow)
                    throw std::logic_error("Project workflows are unavailable.");
                RequireWorkflowSuccess(m_ProjectWorkflow->Add(inspection, KeireHub::HubNowUnixSeconds()));
                KeireHub::ReloadProjectRegistry(m_Registry);
                if (Keire::Project::IsLocked(inspection.Root))
                    throw std::runtime_error("Project is already open in another editor.");
                if (inspection.Status == Keire::ProjectStatus::UpgradeAvailable ||
                    inspection.Status == Keire::ProjectStatus::RecoveryRequired)
                {
                    const auto upgrades = Owner().Modules() ? Owner().Modules()->ProjectUpgrades()
                                                            : std::vector<Keire::ProjectUpgradeStep>{};
                    m_ProjectUpgradeUi.Begin(inspection.Root, upgrades);
                    m_RequestUpgradePopup = true;
                    return;
                }
                if (inspection.Status != Keire::ProjectStatus::Ready &&
                    inspection.Status != Keire::ProjectStatus::RequiresNewerEngine)
                {
                    throw std::runtime_error("The project is not in a state that an installed editor can open.");
                }
                Launch(inspection);
            }
            catch (const std::exception& error)
            {
                ReportUnexpected("The project could not be opened.", error);
            }
        }

        void DrawCreateDialog(Keire::UiFrame& ui)
        {
            const auto request = KeireHub::DrawHubCreateProjectDialog(
                ui, m_ProductSnapshot, m_CreateTemplateId, m_CreateEditorId, m_CreateName, m_CreateLocation,
                m_CreateOpenAfterCreation, static_cast<bool>(m_FolderDialog));
            if (request.Action == KeireHub::HubCreateProjectAction::Browse)
            {
                BrowseForFolder(FolderTarget::CreateLocation, Keire::Detail::PathFromUtf8(m_CreateLocation));
                return;
            }
            if (request.Action != KeireHub::HubCreateProjectAction::Create)
                return;
            try
            {
                if (!m_Templates)
                    throw std::runtime_error("The verified template catalog is unavailable.");
                const auto editor =
                    std::ranges::find(m_ProductSnapshot.Editors, request.EditorId, &KeireHub::HubEditorUiRecord::Id);
                if (editor == m_ProductSnapshot.Editors.end())
                    throw std::runtime_error("The selected editor is no longer installed.");
                std::error_code directoryError;
                std::filesystem::create_directories(request.ParentDirectory, directoryError);
                if (directoryError || !std::filesystem::is_directory(request.ParentDirectory, directoryError) ||
                    directoryError)
                {
                    throw std::runtime_error("The selected project location could not be created or opened.");
                }
                const auto status = m_Templates->StartCreate({.TemplateId = request.TemplateId,
                                                              .ProjectName = request.Name,
                                                              .ParentDirectory = request.ParentDirectory,
                                                              .EditorId = editor->Id,
                                                              .EditorVersion = editor->Version,
                                                              .EditorAssetToolEntrypoint = editor->AssetToolEntrypoint,
                                                              .HostPlatform = editor->Platform,
                                                              .HostArchitecture = editor->Architecture,
                                                              .MinimumProjectSchema = editor->MinimumProjectSchema,
                                                              .MaximumProjectSchema = editor->MaximumProjectSchema});
                RequireWorkflowSuccess(status);
                m_ActiveCreationOpenAfter = request.OpenAfterCreation;
            }
            catch (const std::exception& error)
            {
                ReportUnexpected("The project could not be created.", error);
            }
        }

        void DrawOpenDialog(Keire::UiFrame& ui)
        {
            const auto tokens =
                KeireHub::HubDesignTokens::For(m_ProductSnapshot.Settings.Appearance, KeireHub::HubSystemPrefersDark());
            KeireHub::PrepareHubModal(ui, {620.0F, 300.0F});
            KeireHub::HubModalStyleScope modalStyle(ui, tokens);
            if (auto dialog = ui.BeginPopupModal("Open Project", nullptr, KeireHub::HubModalWindowOptions(), false);
                dialog)
            {
                KeireHub::DrawHubModalHeader(ui, tokens, "Open an existing project",
                                             "Select a Kéire project folder to validate and add to the Hub.",
                                             "PROJECTS");
                ui.TextColored(tokens.SecondaryText, "Project folder");
                ui.SetNextItemWidth(std::max(1.0F, ui.ContentAvailable().Width - 104.0F));
                (void)ui.InputText("##OpenProjectFolder", m_OpenPath);
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(static_cast<bool>(m_FolderDialog)); disabled)
                {
                    if (KeireHub::HubSecondaryButton(ui, tokens, "Browse...", {96.0F, 38.0F}))
                        BrowseForFolder(FolderTarget::OpenProject, Keire::Detail::PathFromUtf8(m_OpenPath));
                }
                ui.Spacing();
                if (KeireHub::HubPrimaryButton(ui, tokens, "Open project", {124.0F, 38.0F}))
                {
                    ui.CloseCurrentPopup();
                    Open(Keire::Detail::PathFromUtf8(m_OpenPath));
                }
                ui.SameLine();
                if (KeireHub::HubSecondaryButton(ui, tokens, "Cancel", {88.0F, 38.0F}))
                    ui.CloseCurrentPopup();
            }
        }

        std::filesystem::path m_Executable;
        bool m_Smoke = false;
        Keire::Ref<Keire::ProjectRegistry> m_Registry;
        std::unique_ptr<KeireHub::HubController> m_Controller;
        std::unique_ptr<KeireHub::HubProjectWorkflow> m_ProjectWorkflow;
        std::unique_ptr<KeireHub::HubProjectMutationWorkflow> m_ProjectMutations;
        std::unique_ptr<KeireHub::HubProjectMetadataWorkflow> m_ProjectMetadata;
        std::unique_ptr<KeireHub::HubEditorManagementWorkflow> m_EditorManagement;
        std::unique_ptr<KeireHub::HubEditorInstallWorkflow> m_EditorInstalls;
        std::unique_ptr<KeireHub::HubFirstRunWorkflow> m_FirstRun;
        std::unique_ptr<KeireHub::HubDistributionWorkflow> m_Distribution;
        KeireHub::HubAccountIntegration m_Account;
        std::unique_ptr<KeireHub::HubPackageTaskWorkflow> m_PackageTasks;
        std::unique_ptr<KeireHub::HubTemplateWorkflow> m_Templates;
        std::unique_ptr<KeireHub::HubBuildSupportIntegration> m_BuildSupport;
        Keire::Ref<Keire::SystemTray> m_Tray;
        Keire::Ref<Keire::FolderDialogOperation> m_FolderDialog;
        KeireHub::HubProductUi m_ProductUi;
        KeireHub::HubProjectsUi m_ProjectsUi;
        KeireHub::HubProjectUpgradeUi m_ProjectUpgradeUi;
        KeireHub::EditorProcessTracker m_EditorProcesses{Keire::Detail::IsProcessAlive};
        KeireHub::HubUpdateHandoffWorkflow m_HubUpdateHandoff;
        KeireHub::HubMaintenanceWorkflow m_Maintenance;
        KeireHub::TaskNotificationTracker m_TaskNotifications;
        KeireHub::HubProductSnapshot m_ProductSnapshot;
        std::optional<PendingStartupActivation> m_PendingStartupActivation;
        std::shared_ptr<KeireHub::HubInstanceCoordinator> m_Instance;
        bool m_DistributionRefreshPending = false;
        bool m_PackageTaskRefreshPending = false;
        FolderTarget m_FolderTarget = FolderTarget::None;
        KeireHub::HubPage m_Page = KeireHub::HubPage::Home;
        std::string m_CreateName = "NewProject";
        std::string m_CreateLocation;
        std::string m_OpenPath;
        std::string m_Notice;
        std::optional<std::string> m_RuntimeStartupFailure;
        std::string m_FatalActionMessage;
        std::string m_ObservedNotice;
        std::string m_LastPersistedError;
        std::string m_PendingEditorInstallVersion;
        std::string m_PendingLocateProjectId;
        std::filesystem::path m_PackageTaskTemporaryRoot;
        std::uint32_t m_Frames = 0;
        std::uint32_t m_PackageTaskConcurrentDownloads = 0;
        std::uint64_t m_HandledTemplateCreation = 0;
        std::uint64_t m_HandledProjectMutation = 0;
        std::uint64_t m_HandledPackageTaskFailureRevision = 0;
        std::chrono::steady_clock::time_point m_NoticeStarted = std::chrono::steady_clock::now();
        std::string m_CreateTemplateId = "keire.3d-starter";
        std::string m_CreateEditorId;
        bool m_NoticeError = false;
        bool m_RequestCreatePopup = false;
        bool m_RequestOpenPopup = false;
        bool m_RequestUpgradePopup = false;
        bool m_SettingsDiscoveryPending = false;
        bool m_CreateOpenAfterCreation = true;
        bool m_ActiveCreationOpenAfter = true;
    };
} // namespace

namespace KeireHub::Detail
{
    std::unique_ptr<Keire::Layer> CreateHubLayer(std::filesystem::path executable, const bool smoke,
                                                 std::optional<HubActivationRequest> pendingStartupActivation,
                                                 std::shared_ptr<HubInstanceCoordinator> instance)
    {
        return std::make_unique<HubLayer>(std::move(executable), smoke, std::move(pendingStartupActivation),
                                          std::move(instance));
    }
} // namespace KeireHub::Detail
