#include "KeireHub/HubBuildSupportIntegration.h"

#include "Keire/Core.h"

#include "KeireHub/HubBuildSupportInventoryWorkflow.h"
#include "KeireHub/HubModalUi.h"

#include "KeireHubRuntime/BuildSupportOperationStore.h"
#include "KeireHubRuntime/EditorInstallationManager.h"

#include "KeireInternal/Build/PlayerPackage.h"
#include "KeireInternal/Build/PlayerSupportPackage.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] BuildSupportEditorTarget ToTarget(const HubEditorUiRecord& editor)
        {
            // Editor management already validated the immutable installation snapshot off the owner thread.
            const bool toolAvailable = !editor.AssetToolEntrypoint.empty();
            return {.InstallationId = editor.Id,
                    .EngineVersion = editor.Version,
                    .Root = editor.Root,
                    .AssetToolEntrypoint = editor.AssetToolEntrypoint,
                    .Healthy = editor.Healthy && toolAvailable,
                    .Running = editor.Running,
                    .HasActiveTask = editor.HasActiveTask};
        }

        [[nodiscard]] std::string HumanBytes(const std::uint64_t bytes)
        {
            constexpr std::uint64_t MiB = 1024ULL * 1024ULL;
            if (bytes < MiB)
                return std::to_string(bytes / 1024ULL) + " KiB";
            return std::to_string(bytes / MiB) + " MiB";
        }

        [[nodiscard]] std::string TaskPhase(const BuildSupportOperationKind kind, const std::string_view phase,
                                            const bool cancelling)
        {
            if (cancelling)
                return "Cancelling";
            if (kind == BuildSupportOperationKind::Remove || phase == "install")
                return "Installing";
            if (phase == "verify")
                return "Verifying";
            if (phase == "complete")
                return "Configuring";
            return "Starting";
        }

        [[nodiscard]] std::uint64_t NowUnixSeconds() noexcept
        {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(
                std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::seconds>(now).count()));
        }

        [[nodiscard]] const char* TaskTitle(const BuildSupportOperationKind kind) noexcept
        {
            switch (kind)
            {
            case BuildSupportOperationKind::Import:
                return "Import Build Support";
            case BuildSupportOperationKind::Repair:
                return "Repair Build Support";
            case BuildSupportOperationKind::Remove:
                return "Remove Build Support";
            }
            return "Build Support";
        }

        [[nodiscard]] std::optional<BuildSupportOperationRecord>
        ActiveOperation(const BuildSupportOperationStore& store)
        {
            const auto snapshot = store.Snapshot();
            const auto found =
                std::ranges::find_if(*snapshot, [](const auto& record) { return IsActive(record.State); });
            return found == snapshot->end() ? std::nullopt : std::optional(*found);
        }

        [[nodiscard]] bool HasSafeOperationDirectory(const BuildSupportOperationRecord& record,
                                                     std::string* diagnostic = nullptr)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(record.OperationRoot, error);
            const bool safe = !error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status) &&
                              (record.StatusPath.empty() || record.StatusPath.parent_path() == record.OperationRoot) &&
                              (record.CancelPath.empty() || record.CancelPath.parent_path() == record.OperationRoot);
            if (!safe && diagnostic)
                *diagnostic = error ? error.message() : "The operation directory or protocol path is unsafe.";
            return safe;
        }

        [[nodiscard]] bool IsMissing(const std::error_code& error, const std::filesystem::file_status status) noexcept
        {
            return error == std::errc::no_such_file_or_directory ||
                   (!error && status.type() == std::filesystem::file_type::not_found);
        }

        [[nodiscard]] bool HasSafeProtocolLeaf(const std::filesystem::path& path, const bool allowMissing,
                                               std::string* diagnostic = nullptr)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (allowMissing && IsMissing(error, status))
                return true;
            const bool safe =
                !error && std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status);
            if (!safe && diagnostic)
                *diagnostic = error ? error.message() : "The operation protocol file is unsafe.";
            return safe;
        }

        struct BuildSupportRecoveryProbeResult final
        {
            std::string OperationId;
            EditorEntrypointActivity EntrypointActivity = EditorEntrypointActivity::Indeterminate;
            bool ProcessIdAlive = false;
            bool StatusPresent = false;
            bool StatusMalformed = false;
            std::optional<Keire::Detail::PlayerBuildStatusDocument> Status;
            std::string Diagnostic;
        };
    } // namespace

    class HubBuildSupportIntegration::Impl final
    {
      public:
        Impl() : m_OperationStore(Keire::Detail::PlayerSupportStorageRoot().parent_path() / "BuildSupportOperations")
        {
            if (const auto loaded = m_OperationStore.Load(); !loaded)
            {
                m_OperationStoreAvailable = false;
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support operation journal could not be loaded [{}]: {}",
                                   ToString(loaded.Error().Code), loaded.Error().TechnicalDetails);
                m_OperationStoreFailureMessage =
                    "Build Support operations are disabled because their task journal could not be loaded. See Hub "
                    "logs for recovery details.";
                SetError(m_OperationStoreFailureMessage);
            }
            else
            {
                for (const auto& record : *m_OperationStore.Snapshot())
                {
                    if (IsTerminal(record.State))
                        CleanupOperationFiles(record.Id);
                }
            }
        }

        HubStatus Refresh() { return m_InventoryWorkflow.Start(); }

        void SynchronizeEditors(HubProductSnapshot& product)
        {
            m_Editors.clear();
            m_Editors.reserve(product.Editors.size());
            for (auto& editor : product.Editors)
            {
                auto target = ToTarget(editor);
                editor.ComponentCount = CountBuildSupportComponents(m_Inventory, editor.Version);
                if (!target.Healthy)
                    editor.AssetToolEntrypoint.clear();
                m_Editors.push_back(std::move(target));
            }
            product.HealthyComponents =
                static_cast<std::size_t>(std::ranges::count_if(m_Inventory, &BuildSupportComponent::Healthy));
            product.UnhealthyComponents = m_Inventory.size() - product.HealthyComponents;
            const auto activeOperation = ActiveOperation(m_OperationStore);
            product.BuildSupportBusy =
                !m_OperationStoreAvailable || activeOperation.has_value() || static_cast<bool>(m_FileDialog);
            product.BuildSupportInventoryLoading = m_InventoryWorkflow.Snapshot()->IsLoading();
            if (product.BuildSupportBusy)
            {
                // Keep the target installation pinned while its Asset Tool or package picker owns the operation.
                // If legacy state has no target, fail closed for all editors until that state clears.
                const auto installationId = activeOperation ? std::string_view(activeOperation->TargetInstallationId)
                                            : m_Selection   ? std::string_view(m_Selection->Editor.InstallationId)
                                                            : std::string_view{};
                for (auto& editor : product.Editors)
                {
                    if (installationId.empty() || editor.Id == installationId)
                        editor.HasActiveTask = true;
                }
            }

            AppendTasks(product);

            if (m_PendingFocus && !m_Editors.empty())
            {
                auto pending = std::exchange(m_PendingFocus, std::nullopt);
                (void)FocusTarget(*pending->Platform, *pending->Architecture);
            }

            if (!m_Selection)
                return;
            auto refreshed =
                SelectBuildSupportEditor(m_Editors, m_Selection->Editor.InstallationId, m_Selection->Filter);
            if (!refreshed)
            {
                m_Selection.reset();
                m_Filtered.clear();
                SetError(refreshed.Error().Message);
                return;
            }
            m_Selection = std::move(refreshed).Value();
            RefreshFiltered();
        }

        HubStatus ManageEditor(const std::string_view installationId)
        {
            if (!m_OperationStoreAvailable)
            {
                m_RequestOpen = true;
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InvalidData, .Message = m_Message, .AffectedItem = "build-support"});
            }
            if (const auto active = ActiveOperation(m_OperationStore);
                active && active->TargetInstallationId != installationId)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                           .Message = "Another editor has an active Build Support operation.",
                                           .Retryable = true,
                                           .AffectedItem = active->TargetInstallationId});
            }
            auto selected = SelectBuildSupportEditor(m_Editors, installationId);
            if (!selected)
            {
                SetError(selected.Error().Message);
                m_RequestOpen = true;
                return HubStatus::Failure(selected.Error());
            }
            m_PendingFocus.reset();
            m_Selection = std::move(selected).Value();
            m_Message = "Build Support is scoped to Kéire Editor " + m_Selection->Editor.EngineVersion + ".";
            m_Error = false;
            m_RequestOpen = true;
            RefreshFiltered();
            return HubStatus::Success();
        }

        HubStatus FocusTarget(const std::string_view platform, const std::string_view architecture)
        {
            if (!m_OperationStoreAvailable)
            {
                m_RequestOpen = true;
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InvalidData, .Message = m_Message, .AffectedItem = "build-support"});
            }
            if (const auto active = ActiveOperation(m_OperationStore); active)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                           .Message = "A Build Support operation is already active.",
                                           .Retryable = true,
                                           .AffectedItem = active->TargetInstallationId});
            }
            auto selected = SelectBuildSupportEditorForTarget(m_Editors, platform, architecture);
            m_RequestOpen = true;
            if (!selected)
            {
                if (selected.Error().Code == HubErrorCode::NotFound && m_Editors.empty())
                {
                    m_PendingFocus = BuildSupportFilter{.Platform = std::string(platform),
                                                        .Architecture = std::string(architecture)};
                    m_Message = "Waiting for editor discovery before opening the requested Build Support target.";
                    m_Error = false;
                    return HubStatus::Success();
                }
                m_PendingFocus.reset();
                m_Selection.reset();
                m_Filtered.clear();
                SetError(selected.Error().Message);
                return HubStatus::Failure(selected.Error());
            }
            m_PendingFocus.reset();
            m_Selection = std::move(selected).Value();
            m_Message = "Showing " + std::string(platform) + " / " + std::string(architecture) +
                        " Build Support for Kéire Editor " + m_Selection->Editor.EngineVersion + ".";
            m_Error = false;
            RefreshFiltered();
            return HubStatus::Success();
        }

        HubStatus ImportPackage(const std::filesystem::path& package)
        {
            if (!m_Selection)
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InvalidTransition,
                     .Message = "Select Manage Components on the target editor before importing Build Support.",
                     .AffectedItem = "build-support"});
            }
            if (!m_OperationStoreAvailable)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                           .Message = "The Build Support task journal must be repaired first.",
                                           .AffectedItem = "build-support"});
            }
            if (ActiveOperation(m_OperationStore) || m_FileDialog)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                           .Message = "Another Build Support operation is already active.",
                                           .Retryable = true,
                                           .AffectedItem = m_Selection->Editor.InstallationId});
            }
            StartImport(package);
            if (!m_Process)
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InvalidArgument,
                     .Message = m_Message.empty() ? "Build Support import could not start." : m_Message,
                     .AffectedItem = m_Selection->Editor.InstallationId});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] bool OwnsTask(const std::string_view taskId) const noexcept
        {
            const auto snapshot = m_OperationStore.Snapshot();
            return std::ranges::any_of(*snapshot,
                                       [&](const auto& record) { return BuildSupportTaskId(record.Id) == taskId; });
        }

        HubStatus ExecuteTaskCommand(const HubUiCommand& command)
        {
            if (!OwnsTask(command.ItemId))
            {
                return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                           .Message = "The Build Support task is no longer available.",
                                           .AffectedItem = command.ItemId});
            }
            const auto active = ActiveOperation(m_OperationStore);
            if (command.Type != HubUiCommandType::CancelTask || !active ||
                BuildSupportTaskId(active->Id) != command.ItemId || active->Kind == BuildSupportOperationKind::Remove ||
                active->State == BuildSupportOperationState::Cancelling)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                           .Message = "This Build Support task cannot perform that action.",
                                           .AffectedItem = command.ItemId});
            }
            if (!CancelOperation(*active))
            {
                return HubStatus::Failure({.Code = HubErrorCode::IoWrite,
                                           .Message = "The Build Support cancellation request could not be written.",
                                           .Retryable = true,
                                           .AffectedItem = command.ItemId});
            }
            return HubStatus::Success();
        }

        void Poll()
        {
            PollInventory();
            PollFileDialog();
            PollProcess();
            PollRecoveredOperation();
            ReconcileRemoval();
        }

        void Draw(Keire::UiFrame& ui, Keire::WindowSystem& windows, const Keire::WindowId window,
                  const HubDesignTokens& tokens, const bool offlineMode)
        {
            if (std::exchange(m_RequestOpen, false))
                ui.OpenPopup("Manage Build Support");
            {
                PrepareHubModal(ui, {760.0F, 600.0F});
                HubModalStyleScope modalStyle(ui, tokens);
                if (auto modal = ui.BeginPopupModal("Manage Build Support", nullptr, HubModalWindowOptions(), false);
                    modal)
                {
                    const auto subtitle =
                        m_Selection ? "Install, verify, repair, or remove platform modules for Kéire Editor " +
                                          m_Selection->Editor.EngineVersion + '.'
                                    : "Select a healthy editor installation before managing platform modules.";
                    DrawHubModalHeader(ui, tokens, "Manage Build Support", subtitle, "EDITOR COMPONENTS");
                    DrawPanel(ui, windows, window, tokens, offlineMode);
                    ui.Spacing();
                    ui.Separator();
                    ui.Spacing();
                    if (HubSecondaryButton(ui, tokens, "Close##BuildSupport", {96.0F, 36.0F}))
                        ui.CloseCurrentPopup();
                }
            }
            if (std::exchange(m_RequestRemoveConfirmation, false))
                ui.OpenPopup("Remove Build Support?");
            {
                PrepareHubModal(ui, {580.0F, 330.0F});
                HubModalStyleScope confirmationStyle(ui, tokens);
                if (auto confirmation =
                        ui.BeginPopupModal("Remove Build Support?", nullptr, HubModalWindowOptions(), false);
                    confirmation)
                {
                    DrawRemovalConfirmation(ui, tokens);
                }
            }
        }

        void Stop() noexcept
        {
            m_InventoryWorkflow.Stop();
            m_FileDialog.Reset();
            if (m_RecoveryProbe.valid())
            {
                try
                {
                    m_RecoveryProbe.wait();
                }
                catch (...)
                {
                }
            }
            if (m_RemovalJournalProbe.valid())
            {
                try
                {
                    m_RemovalJournalProbe.wait();
                }
                catch (...)
                {
                }
            }
            if (!m_Process)
                return;
            try
            {
                if (m_Operation && m_Operation->Kind != BuildSupportOperationKind::Remove)
                    CancelInstall();
                const auto wait = m_Operation && m_Operation->Kind == BuildSupportOperationKind::Remove
                                      ? std::chrono::seconds(5)
                                      : std::chrono::milliseconds(500);
                if (!m_Process->WaitFor(wait))
                    m_Process->Terminate();
            }
            catch (...)
            {
                m_Process->Terminate();
            }
            m_Process.reset();
            m_Operation.reset();
            m_OwnedOperationId.clear();
        }

      private:
        void PollInventory()
        {
            const auto polled = m_InventoryWorkflow.Poll();
            if (!polled)
            {
                SetError(polled.Error().Message);
                return;
            }
            const auto inventory = m_InventoryWorkflow.Snapshot();
            if (inventory->Revision == m_AppliedInventoryRevision)
                return;
            m_AppliedInventoryRevision = inventory->Revision;
            if (inventory->State == HubBuildSupportInventoryState::Ready && inventory->Components)
            {
                m_Inventory = *inventory->Components;
                RefreshFiltered();
            }
            else if (inventory->State == HubBuildSupportInventoryState::Failed && inventory->Failure)
            {
                if (!inventory->Failure->TechnicalDetails.empty())
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Build Support inventory refresh failed [{}]: {}",
                                       ToString(inventory->Failure->Code), inventory->Failure->TechnicalDetails);
                }
                SetError(inventory->Failure->Message);
            }
        }

        void AppendTasks(HubProductSnapshot& product)
        {
            const auto snapshot = m_OperationStore.Snapshot();
            for (const auto& record : *snapshot)
            {
                const auto taskId = BuildSupportTaskId(record.Id);
                std::erase_if(product.Tasks,
                              [&](const HubTaskUiRecord& task) { return task.Id == std::string_view(taskId); });
                product.Tasks.push_back({.Id = taskId,
                                         .Title = TaskTitle(record.Kind),
                                         .Phase = record.Phase,
                                         .Message = record.Message,
                                         .CurrentPackage = record.CurrentPackage,
                                         .Progress = record.Progress,
                                         .Active = IsActive(record.State),
                                         .Cancellable = IsActive(record.State) &&
                                                        record.State != BuildSupportOperationState::Cancelling &&
                                                        record.Kind != BuildSupportOperationKind::Remove});
            }
        }

        [[nodiscard]] HubStatus UpdateActiveTask()
        {
            if (m_OwnedOperationId.empty() || !m_Operation)
                return HubStatus::Success();
            auto message =
                BuildSupportLiveStatusText(m_Operation->Kind, m_Status.State, m_Status.Phase, m_Status.ErrorCode);
            if (m_CancelRequested)
                message = "Cancelling the Build Support operation.";
            return m_OperationStore.Update(
                m_OwnedOperationId,
                m_CancelRequested ? BuildSupportOperationState::Cancelling : BuildSupportOperationState::Running,
                TaskPhase(m_Operation->Kind, m_Status.Phase, m_CancelRequested), std::move(message),
                std::clamp(m_Status.Progress, 0.0F, 1.0F), NowUnixSeconds());
        }

        [[nodiscard]] HubStatus FinishTask(const std::string_view operationId, const BuildSupportOperationState state,
                                           std::string phase, std::string message, const bool completed)
        {
            const auto status =
                m_OperationStore.Finish(std::string(operationId), state, std::move(phase), std::move(message),
                                        completed ? 1.0F : m_Status.Progress, NowUnixSeconds());
            if (status)
                CleanupOperationFiles(operationId);
            return status;
        }

        void RefreshFiltered()
        {
            m_Filtered = m_Selection ? FilterBuildSupportComponents(m_Inventory, *m_Selection)
                                     : std::vector<BuildSupportComponent>{};
        }

        void SetError(std::string message)
        {
            m_Message = !m_OperationStoreAvailable && !m_OperationStoreFailureMessage.empty()
                            ? m_OperationStoreFailureMessage
                            : std::move(message);
            m_Error = true;
        }

        void BrowseForPackage(Keire::WindowSystem& windows, const Keire::WindowId window,
                              std::optional<std::string> repairComponent = {})
        {
            if (!m_Selection || m_FileDialog || ActiveOperation(m_OperationStore) || !m_OperationStoreAvailable)
                return;
            try
            {
                m_RepairComponent = std::move(repairComponent);
                m_FileDialog = windows.ShowOpenFileDialog(
                    window, {.Title = m_RepairComponent ? "Repair Kéire Build Support" : "Import Kéire Build Support",
                             .DefaultLocation = std::filesystem::current_path(),
                             .FilterName = "Kéire Build Support",
                             .Extension = "keireplayersupport"});
            }
            catch (const std::exception& error)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support file picker failed: {}", error.what());
                SetError("The Build Support file picker could not be opened. See Hub logs for details.");
            }
        }

        void PollFileDialog()
        {
            if (!m_FileDialog)
                return;
            const auto status = m_FileDialog->Status();
            if (status == Keire::OpenFileDialogStatus::Pending)
                return;
            if (status == Keire::OpenFileDialogStatus::Selected)
            {
                const auto package = m_FileDialog->SelectedPath();
                m_FileDialog.Reset();
                StartImport(package);
            }
            else if (status == Keire::OpenFileDialogStatus::Failed)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support file picker failed: {}", m_FileDialog->Diagnostic());
                m_FileDialog.Reset();
                m_RepairComponent.reset();
                SetError("The Build Support file picker failed. See Hub logs for details.");
            }
            else
            {
                m_FileDialog.Reset();
                m_RepairComponent.reset();
            }
        }

        void StartImport(const std::filesystem::path& selectedPackage)
        {
            if (!m_Selection || ActiveOperation(m_OperationStore) || !m_OperationStoreAvailable)
                return;
            try
            {
                const auto package = std::filesystem::absolute(selectedPackage).lexically_normal();
                std::error_code error;
                if (!std::filesystem::is_regular_file(package, error) || error)
                    throw std::invalid_argument("Select a regular .keireplayersupport Build Support package.");
                const BuildSupportComponent* repair = nullptr;
                if (m_RepairComponent)
                {
                    const auto found = std::ranges::find(m_Filtered, *m_RepairComponent, &BuildSupportComponent::Id);
                    if (found == m_Filtered.end())
                        throw std::runtime_error("The selected Build Support repair target is no longer installed.");
                    repair = &*found;
                }
                const auto operationId = Keire::AssetId::Generate().ToString();
                const auto operation = m_OperationStore.Root() / operationId;
                auto plan = PlanBuildSupportImport(*m_Selection, package, operation, repair);
                if (!plan)
                    throw std::runtime_error(plan.Error().Message);
                const auto launch = LaunchOperation(std::move(plan).Value(), operationId,
                                                    Keire::Detail::PathToUtf8(package.filename()));
                if (!launch)
                    throw std::runtime_error(launch.Error().Message);
                m_RepairComponent.reset();
            }
            catch (const std::exception& error)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support import could not start: {}", error.what());
                m_RepairComponent.reset();
                SetError("Build Support import could not start. See Hub logs for details.");
            }
        }

        void StartRemoval(const BuildSupportComponent& component)
        {
            if (!m_Selection || ActiveOperation(m_OperationStore) || !m_OperationStoreAvailable)
                return;
            try
            {
                auto plan = PlanBuildSupportRemoval(*m_Selection, component);
                if (!plan)
                    throw std::runtime_error(plan.Error().Message);
                std::error_code error;
                if (!std::filesystem::is_regular_file(plan.Value().Executable, error) || error)
                    throw std::runtime_error("The selected editor Asset Tool is unavailable.");
                const auto operationId = Keire::AssetId::Generate().ToString();
                const auto launch = LaunchOperation(std::move(plan).Value(), operationId, component.Id);
                if (!launch)
                    throw std::runtime_error(launch.Error().Message);
            }
            catch (const std::exception& error)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support removal could not start: {}", error.what());
                SetError("Build Support removal could not start. See Hub logs for details.");
            }
        }

        [[nodiscard]] HubStatus LaunchOperation(BuildSupportCommandPlan plan, const std::string& operationId,
                                                std::string currentPackage)
        {
            if (!m_Selection || ActiveOperation(m_OperationStore))
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::InstallationBusy,
                     .Message = "Another Build Support operation is already active.",
                     .Retryable = true,
                     .AffectedItem = m_Selection ? m_Selection->Editor.InstallationId : std::string("build-support")});
            }
            const auto now = NowUnixSeconds();
            const auto operationRoot = m_OperationStore.Root() / operationId;
            const auto initialPhase = plan.Kind == BuildSupportOperationKind::Remove ? "Installing" : "Starting";
            const auto statusPhase = plan.Kind == BuildSupportOperationKind::Remove ? "remove" : "start";
            const auto initialMessage = BuildSupportLiveStatusText(plan.Kind, "running", statusPhase);
            BuildSupportOperationRecord record{.Id = operationId,
                                               .Kind = plan.Kind,
                                               .State = BuildSupportOperationState::Launching,
                                               .TargetInstallationId = m_Selection->Editor.InstallationId,
                                               .EngineVersion = m_Selection->Editor.EngineVersion,
                                               .EditorRoot = m_Selection->Editor.Root,
                                               .AssetToolEntrypoint = plan.Executable,
                                               .OperationRoot = operationRoot,
                                               .StatusPath = plan.StatusPath,
                                               .CancelPath = plan.CancelPath,
                                               .ComponentId = plan.ComponentId,
                                               .CurrentPackage = std::move(currentPackage),
                                               .Phase = initialPhase,
                                               .Message = initialMessage,
                                               .Progress = 0.0F,
                                               .CreatedUnixSeconds = now,
                                               .UpdatedUnixSeconds = now};
            if (const auto added = m_OperationStore.Add(record); !added)
                return added;
            bool directoryCreated = false;
            try
            {
                directoryCreated = std::filesystem::create_directory(operationRoot);
                if (!directoryCreated)
                    throw std::runtime_error("The Build Support operation directory already exists.");
                auto process =
                    Keire::Detail::ChildProcess::Start(plan.Executable, plan.Arguments, plan.WorkingDirectory);
                const auto attached =
                    m_OperationStore.AttachProcess(operationId, process.ProcessId(), NowUnixSeconds());
                if (!attached)
                {
                    process.Terminate();
                    const auto failed = m_OperationStore.Finish(
                        operationId, BuildSupportOperationState::Failed, "Failed",
                        "Build Support could not start because its child process identity was not persisted.", 0.0F,
                        NowUnixSeconds());
                    if (!failed)
                    {
                        KEIRE_CLIENT_ERROR("[Project Hub] Build Support launch rollback could not be journaled: {}",
                                           failed.Error().TechnicalDetails);
                    }
                    throw std::runtime_error(attached.Error().Message);
                }
                m_Process.emplace(std::move(process));
                m_Operation = std::move(plan);
                m_OwnedOperationId = operationId;
                m_Status = {.State = "running", .Phase = statusPhase, .Progress = 0.0F, .Message = initialMessage};
                m_CancelRequested = false;
                m_Message = m_Operation->Kind == BuildSupportOperationKind::Remove
                                ? "Removing Build Support " + m_Operation->ComponentId + "."
                            : m_Operation->Kind == BuildSupportOperationKind::Repair
                                ? "Verifying and repairing Build Support from the selected package."
                                : "Verifying and importing the selected Build Support package.";
                m_Error = false;
                return HubStatus::Success();
            }
            catch (const std::exception& error)
            {
                const auto active = ActiveOperation(m_OperationStore);
                if (active && active->Id == operationId)
                {
                    const auto failed = m_OperationStore.Finish(
                        operationId, BuildSupportOperationState::Failed, "Failed",
                        "The selected editor Asset Tool could not start the Build Support operation.", 0.0F,
                        NowUnixSeconds());
                    if (!failed)
                    {
                        KEIRE_CLIENT_ERROR("[Project Hub] Build Support launch failure could not be journaled: {}",
                                           failed.Error().TechnicalDetails);
                    }
                }
                if (directoryCreated)
                    CleanupOperationFiles(operationId);
                return HubStatus::Failure({.Code = HubErrorCode::ProcessLaunchFailed,
                                           .Message = "The selected editor Asset Tool could not start.",
                                           .AffectedItem = record.TargetInstallationId,
                                           .TechnicalDetails = error.what()});
            }
        }

        [[nodiscard]] bool CancelOperation(const BuildSupportOperationRecord& record) noexcept
        {
            if (record.Kind == BuildSupportOperationKind::Remove || record.CancelPath.empty() ||
                record.State == BuildSupportOperationState::Cancelling || !IsActive(record.State))
            {
                return false;
            }
            try
            {
                std::string diagnostic;
                if (!HasSafeOperationDirectory(record, &diagnostic) ||
                    !HasSafeProtocolLeaf(record.CancelPath, true, &diagnostic))
                    throw std::runtime_error(diagnostic);
                Keire::Detail::WriteTextFileAtomically(record.CancelPath, "cancel\n");
                const auto updated = m_OperationStore.Update(record.Id, BuildSupportOperationState::Cancelling,
                                                             "Cancelling", "Cancelling the Build Support operation.",
                                                             record.Progress, NowUnixSeconds());
                if (!updated)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Build Support cancellation could not be journaled: {}",
                                       updated.Error().TechnicalDetails);
                    SetError("The Build Support cancellation request was sent, but its task journal could not be "
                             "updated. See Hub logs for details.");
                }
                m_CancelRequested = true;
                m_Message = "Cancelling the Build Support operation.";
                return true;
            }
            catch (const std::exception& error)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support cancellation could not be written: {}", error.what());
                SetError("The Build Support cancellation request could not be written.");
                return false;
            }
        }

        void CancelInstall() noexcept
        {
            const auto active = ActiveOperation(m_OperationStore);
            if (active)
                (void)CancelOperation(*active);
        }

        void PollProcess()
        {
            if (!m_Process || !m_Operation || m_OwnedOperationId.empty())
                return;
            if (m_Operation->Kind != BuildSupportOperationKind::Remove)
            {
                try
                {
                    std::string diagnostic;
                    if (HasSafeProtocolLeaf(m_Operation->StatusPath, false, &diagnostic))
                        m_Status = Keire::Detail::ReadPlayerBuildStatusDocument(m_Operation->StatusPath);
                    if (const auto updated = UpdateActiveTask(); !updated)
                    {
                        KEIRE_CLIENT_ERROR("[Project Hub] Build Support task progress could not be persisted: {}",
                                           updated.Error().TechnicalDetails);
                    }
                }
                catch (const std::exception&)
                {
                    // The selected Asset Tool replaces status documents atomically; its terminal state is
                    // authoritative.
                }
            }
            if (!m_Process->Poll())
                return;
            auto process = std::move(*m_Process);
            m_Process.reset();
            const auto output = process.TakeOutput();
            const auto exitCode = process.ExitCode().value_or(127);
            if (exitCode != 0)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support Asset Tool failed with exit code {}: {}", exitCode,
                                   output);
            }
            m_Operation.reset();
            m_OwnedOperationId.clear();
            m_NextRecoveryProbe = {};
        }

        void CleanupOperationFiles(const std::string_view operationId) noexcept
        {
            const auto snapshot = m_OperationStore.Snapshot();
            const auto found = std::ranges::find(*snapshot, operationId, &BuildSupportOperationRecord::Id);
            if (found == snapshot->end() || found->OperationRoot != m_OperationStore.Root() / found->Id)
                return;
            std::error_code rootError;
            const auto rootStatus = std::filesystem::symlink_status(found->OperationRoot, rootError);
            if (rootError || !std::filesystem::is_directory(rootStatus) || std::filesystem::is_symlink(rootStatus))
            {
                if (rootError && rootError != std::errc::no_such_file_or_directory)
                {
                    KEIRE_CLIENT_WARN("[Project Hub] Build Support operation directory could not be inspected for {}: "
                                      "{}",
                                      found->Id, rootError.message());
                }
                return;
            }
            const auto removeFile = [&](const std::filesystem::path& path)
            {
                if (path.empty())
                    return;
                std::error_code error;
                (void)std::filesystem::remove(path, error);
                if (error && error != std::errc::no_such_file_or_directory)
                {
                    KEIRE_CLIENT_WARN("[Project Hub] Build Support operation file could not be removed for {}: {}",
                                      found->Id, error.message());
                }
            };
            removeFile(found->StatusPath);
            removeFile(found->CancelPath);
            std::error_code error;
            (void)std::filesystem::remove(found->OperationRoot, error);
            if (error && error != std::errc::no_such_file_or_directory)
            {
                KEIRE_CLIENT_WARN("[Project Hub] Build Support operation directory was not empty for {}: {}", found->Id,
                                  error.message());
            }
        }

        void FinishRecoveredOperation(const BuildSupportOperationRecord& record, const BuildSupportOperationState state,
                                      std::string phase, std::string message, const bool completed)
        {
            m_Status.Progress = completed ? 1.0F : record.Progress;
            if (const auto finished = FinishTask(record.Id, state, std::move(phase), message, completed); !finished)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support terminal task state could not be persisted [{}]: {}",
                                   ToString(finished.Error().Code), finished.Error().TechnicalDetails);
                SetError("The Build Support operation finished, but its task journal could not be updated. See Hub "
                         "logs for details.");
                return;
            }
            m_Message = std::move(message);
            m_Error = state == BuildSupportOperationState::Failed;
            m_CancelRequested = false;
            if (completed && record.Kind != BuildSupportOperationKind::Remove)
            {
                if (const auto refreshed = m_InventoryWorkflow.RequestRefresh(); !refreshed)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Completed Build Support inventory refresh could not start: {}",
                                       refreshed.Error().TechnicalDetails);
                    SetError("Build Support completed, but installed components could not be refreshed. See Hub logs "
                             "for details.");
                }
            }
        }

        void ApplyRecoveryProbe(const BuildSupportOperationRecord& record, const BuildSupportRecoveryProbeResult& probe)
        {
            if (probe.Status)
            {
                m_Status = *probe.Status;
                if (probe.Status->State == "succeeded")
                {
                    const auto message = record.Kind == BuildSupportOperationKind::Repair
                                             ? "Build Support was repaired from the verified package."
                                             : "Build Support was imported from the verified package.";
                    FinishRecoveredOperation(record, BuildSupportOperationState::Completed, "Completed", message, true);
                    return;
                }
                if (probe.Status->State == "failed")
                {
                    const bool cancelled = record.State == BuildSupportOperationState::Cancelling;
                    const auto message = cancelled
                                             ? "Build Support operation cancelled."
                                             : BuildSupportLiveStatusText(record.Kind, probe.Status->State,
                                                                          probe.Status->Phase, probe.Status->ErrorCode);
                    FinishRecoveredOperation(
                        record, cancelled ? BuildSupportOperationState::Cancelled : BuildSupportOperationState::Failed,
                        cancelled ? "Cancelled" : "Failed", message, false);
                    return;
                }
                const auto message = record.State == BuildSupportOperationState::Cancelling
                                         ? "Cancelling the Build Support operation."
                                         : BuildSupportLiveStatusText(record.Kind, probe.Status->State,
                                                                      probe.Status->Phase, probe.Status->ErrorCode);
                const auto updated = m_OperationStore.Update(
                    record.Id, record.State,
                    TaskPhase(record.Kind, probe.Status->Phase, record.State == BuildSupportOperationState::Cancelling),
                    message, std::clamp(probe.Status->Progress, 0.0F, 1.0F), NowUnixSeconds());
                if (!updated)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Recovered Build Support progress could not be persisted: {}",
                                       updated.Error().TechnicalDetails);
                }
            }

            if (probe.EntrypointActivity == EditorEntrypointActivity::Indeterminate)
                return;
            const bool running = probe.EntrypointActivity == EditorEntrypointActivity::Running &&
                                 (!record.ChildProcessId || probe.ProcessIdAlive);
            if (running)
                return;
            if (record.Kind == BuildSupportOperationKind::Remove)
            {
                BeginRemovalReconciliation(record);
                return;
            }
            if (probe.StatusMalformed && !probe.Diagnostic.empty())
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Recovered Build Support status is malformed: {}", probe.Diagnostic);
            }
            const bool cancelled = record.State == BuildSupportOperationState::Cancelling;
            FinishRecoveredOperation(
                record, cancelled ? BuildSupportOperationState::Cancelled : BuildSupportOperationState::Failed,
                cancelled ? "Cancelled" : "Failed",
                cancelled ? "Build Support operation cancelled."
                          : "The Build Support operation was interrupted before publishing a terminal status.",
                false);
        }

        void PollRecoveredOperation()
        {
            if (!m_OperationStoreAvailable)
                return;
            const auto active = ActiveOperation(m_OperationStore);
            if (!active)
                return;
            if (m_Process && active->Id == m_OwnedOperationId)
                return;
            if (m_RemovalReconciliationOperationId == active->Id)
                return;

            if (m_RecoveryProbe.valid())
            {
                if (m_RecoveryProbe.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    return;
                try
                {
                    const auto result = m_RecoveryProbe.get();
                    const auto current = ActiveOperation(m_OperationStore);
                    if (current && current->Id == result.OperationId)
                        ApplyRecoveryProbe(*current, result);
                }
                catch (const std::exception& error)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Build Support recovery probe failed: {}", error.what());
                }
                catch (...)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Build Support recovery probe failed unexpectedly.");
                }
                m_NextRecoveryProbe = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                return;
            }
            if (std::chrono::steady_clock::now() < m_NextRecoveryProbe)
                return;
            try
            {
                const auto record = *active;
                m_RecoveryProbe = std::async(
                    std::launch::async,
                    [record]
                    {
                        BuildSupportRecoveryProbeResult result{.OperationId = record.Id};
                        result.EntrypointActivity = ProbeEditorEntrypointProcessActivity(record.AssetToolEntrypoint);
                        result.ProcessIdAlive =
                            record.ChildProcessId && Keire::Detail::IsProcessAlive(*record.ChildProcessId);
                        if (!HasSafeOperationDirectory(record, &result.Diagnostic))
                        {
                            result.StatusMalformed = true;
                            return result;
                        }
                        if (record.Kind == BuildSupportOperationKind::Remove)
                            return result;
                        std::error_code error;
                        const auto status = std::filesystem::symlink_status(record.StatusPath, error);
                        if (IsMissing(error, status))
                            return result;
                        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))
                        {
                            result.StatusMalformed = true;
                            result.Diagnostic = error ? error.message() : "The status document is unsafe.";
                            return result;
                        }
                        result.StatusPresent = true;
                        try
                        {
                            result.Status = Keire::Detail::ReadPlayerBuildStatusDocument(record.StatusPath);
                        }
                        catch (const std::exception& statusError)
                        {
                            result.StatusMalformed = true;
                            result.Diagnostic = statusError.what();
                        }
                        return result;
                    });
            }
            catch (const std::exception& error)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support recovery worker could not start: {}", error.what());
                m_NextRecoveryProbe = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
        }

        void BeginRemovalReconciliation(const BuildSupportOperationRecord& record)
        {
            if (m_RemovalReconciliationOperationId == record.Id)
                return;
            const auto inventory = m_InventoryWorkflow.Snapshot();
            m_RemovalReconciliationOperationId = record.Id;
            m_RemovalInventoryGate = {.BaselineRevision = inventory->Revision,
                                      .RefreshAfterCurrentLoad = inventory->IsLoading()};
            m_RemovalJournalProbe = {};
            if (!inventory->IsLoading())
            {
                if (const auto refreshed = Refresh(); !refreshed)
                {
                    FinishRecoveredOperation(
                        record, BuildSupportOperationState::Failed, "Failed",
                        "Build Support removal ended, but installed component state could not be refreshed.", false);
                    m_RemovalReconciliationOperationId.clear();
                    return;
                }
            }
            const auto updated = m_OperationStore.Update(record.Id, record.State, "Verifying",
                                                         "Confirming the Build Support removal result.",
                                                         record.Progress, NowUnixSeconds());
            if (!updated)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support removal reconciliation could not be journaled: {}",
                                   updated.Error().TechnicalDetails);
            }
        }

        void ReconcileRemoval()
        {
            if (m_RemovalReconciliationOperationId.empty())
                return;
            const auto active = ActiveOperation(m_OperationStore);
            if (!active || active->Id != m_RemovalReconciliationOperationId)
            {
                m_RemovalReconciliationOperationId.clear();
                return;
            }
            const auto inventory = m_InventoryWorkflow.Snapshot();
            const auto action = EvaluateBuildSupportRemovalInventory(m_RemovalInventoryGate, inventory->Revision,
                                                                     inventory->IsLoading());
            if (action == BuildSupportRemovalInventoryAction::Wait)
                return;
            if (action == BuildSupportRemovalInventoryAction::StartFreshRefresh)
            {
                m_RemovalInventoryGate = {.BaselineRevision = inventory->Revision, .RefreshAfterCurrentLoad = false};
                if (const auto refreshed = Refresh(); !refreshed)
                {
                    FinishRecoveredOperation(
                        *active, BuildSupportOperationState::Failed, "Failed",
                        "Build Support removal ended, but a fresh installed-component scan could not start.", false);
                    m_RemovalReconciliationOperationId.clear();
                }
                return;
            }
            if (inventory->State == HubBuildSupportInventoryState::Failed)
            {
                FinishRecoveredOperation(
                    *active, BuildSupportOperationState::Failed, "Failed",
                    "Build Support removal ended, but its recovery journal or installed inventory is invalid.", false);
                m_RemovalReconciliationOperationId.clear();
                return;
            }
            if (inventory->State != HubBuildSupportInventoryState::Ready || !inventory->Components)
                return;
            if (!m_RemovalJournalProbe.valid())
            {
                const auto storageRoot = Keire::Detail::PlayerSupportStorageRoot();
                const auto engineVersion = active->EngineVersion;
                const auto componentId = active->ComponentId;
                try
                {
                    m_RemovalJournalProbe = std::async(
                        std::launch::async, [storageRoot, engineVersion, componentId]
                        { return HasPendingBuildSupportRemovalJournal(storageRoot, engineVersion, componentId); });
                }
                catch (const std::exception& error)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] Build Support removal journal probe could not start: {}",
                                       error.what());
                    FinishRecoveredOperation(*active, BuildSupportOperationState::Failed, "Failed",
                                             "Build Support removal recovery could not start.", false);
                    m_RemovalReconciliationOperationId.clear();
                }
                return;
            }
            if (m_RemovalJournalProbe.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            HubResult<bool> pending =
                HubResult<bool>::Failure({.Code = HubErrorCode::WorkerInterrupted,
                                          .Message = "Build Support removal recovery did not return a result.",
                                          .AffectedItem = active->ComponentId});
            try
            {
                pending = m_RemovalJournalProbe.get();
            }
            catch (const std::exception& error)
            {
                pending = HubResult<bool>::Failure({.Code = HubErrorCode::WorkerInterrupted,
                                                    .Message = "Build Support removal recovery failed.",
                                                    .AffectedItem = active->ComponentId,
                                                    .TechnicalDetails = error.what()});
            }
            if (!pending)
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Build Support removal journal is invalid: {}",
                                   pending.Error().TechnicalDetails);
                FinishRecoveredOperation(*active, BuildSupportOperationState::Failed, "Failed",
                                         "Build Support removal recovery could not validate its journal state.", false);
                m_RemovalReconciliationOperationId.clear();
                return;
            }
            const bool installed = std::ranges::any_of(
                *inventory->Components, [&](const auto& component)
                { return component.Id == active->ComponentId && component.EngineVersion == active->EngineVersion; });
            if (!pending.Value() && !installed)
            {
                FinishRecoveredOperation(*active, BuildSupportOperationState::Completed, "Completed",
                                         "Build Support " + active->ComponentId + " was removed.", true);
            }
            else
            {
                FinishRecoveredOperation(*active, BuildSupportOperationState::Failed, "Failed",
                                         pending.Value()
                                             ? "Build Support removal requires recovery before it can be completed."
                                             : "The selected Build Support component is still installed.",
                                         false);
            }
            m_RemovalReconciliationOperationId.clear();
        }

        void DrawPanel(Keire::UiFrame& ui, Keire::WindowSystem& windows, const Keire::WindowId window,
                       const HubDesignTokens& tokens, const bool offlineMode)
        {
            const bool inventoryLoading = m_InventoryWorkflow.Snapshot()->IsLoading();
            const auto active = ActiveOperation(m_OperationStore);
            const bool mutationBlocked =
                !m_OperationStoreAvailable || active.has_value() || static_cast<bool>(m_FileDialog) || inventoryLoading;
            if (!m_Selection)
            {
                ui.TextColoredWrapped(tokens.Warning, m_Message);
                ui.TextColoredWrapped(tokens.SecondaryText,
                                      "Select Manage Components on a healthy editor with a typed Asset Tool.");
                return;
            }

            {
                [[maybe_unused]] const auto background =
                    ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Elevated);
                if (auto summary = ui.BeginChild("BuildSupportEditorSummary", {0.0F, 86.0F}, true); summary)
                {
                    ui.TextColored(tokens.PrimaryText, "Kéire Editor " + m_Selection->Editor.EngineVersion);
                    ui.TextColoredWrapped(tokens.SecondaryText, m_Selection->Editor.InstallationId);
                    ui.TextColoredWrapped(tokens.MutedText,
                                          "Verified actions run through this editor's typed Asset Tool.");
                }
            }

            if (m_Selection->Filter.Platform && m_Selection->Filter.Architecture)
            {
                ui.TextColored(tokens.Warning, "Target filter  •  " + *m_Selection->Filter.Platform + " / " +
                                                   *m_Selection->Filter.Architecture);
                if (auto disabled = ui.BeginDisabled(mutationBlocked); disabled)
                {
                    if (HubSecondaryButton(ui, tokens, "Clear filter", {112.0F, 32.0F}))
                    {
                        auto selected = SelectBuildSupportEditor(m_Editors, m_Selection->Editor.InstallationId);
                        if (selected)
                        {
                            m_Selection = std::move(selected).Value();
                            RefreshFiltered();
                        }
                    }
                }
            }
            if (offlineMode)
            {
                ui.TextColoredWrapped(
                    tokens.Warning, "Offline mode: local .keireplayersupport Build Support imports remain available.");
            }
            if (inventoryLoading)
                ui.TextColored(tokens.MutedText, "Refreshing installed component health...");

            ui.Spacing();
            if (auto disabled = ui.BeginDisabled(mutationBlocked); disabled)
            {
                if (HubPrimaryButton(ui, tokens, "Import package...", {154.0F, 36.0F}))
                    BrowseForPackage(windows, window);
            }
            ui.SameLine();
            if (auto disabled = ui.BeginDisabled(mutationBlocked); disabled)
            {
                if (HubSecondaryButton(ui, tokens, "Refresh", {112.0F, 36.0F}))
                    (void)Refresh();
            }

            if (active)
            {
                ui.Spacing();
                ui.TextColoredWrapped(tokens.SecondaryText, active->Message);
                const auto percentage = std::to_string(static_cast<unsigned>(
                                            std::round(std::clamp(active->Progress, 0.0F, 1.0F) * 100.0F))) +
                                        '%';
                ui.TextColored(tokens.PrimaryText, percentage);
                ui.ProgressBar(active->Progress, {0.0F, 8.0F}, " ");
                if (active->Kind != BuildSupportOperationKind::Remove)
                {
                    const bool cancelling = active->State == BuildSupportOperationState::Cancelling;
                    if (auto disabled = ui.BeginDisabled(cancelling); disabled)
                    {
                        if (HubSecondaryButton(ui, tokens, cancelling ? "Cancelling..." : "Cancel", {112.0F, 32.0F}))
                            (void)CancelOperation(*active);
                    }
                }
            }
            if (!m_Message.empty())
            {
                ui.Spacing();
                ui.TextColoredWrapped(m_Error ? tokens.Danger : tokens.Success, m_Message);
            }

            ui.Spacing();
            ui.Separator();
            ui.Spacing();
            ui.TextColored(tokens.PrimaryText, "Installed components");
            if (m_Filtered.empty())
            {
                ui.TextColoredWrapped(tokens.SecondaryText,
                                      "No installed Build Support matches this editor version and target filter.");
                return;
            }
            for (const auto& component : m_Filtered)
            {
                auto id = ui.PushId(component.Id);
                [[maybe_unused]] const auto background =
                    ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Elevated);
                if (auto card =
                        ui.BeginChild("BuildSupportComponent", {0.0F, component.Healthy ? 132.0F : 168.0F}, true);
                    card)
                {
                    ui.TextColored(tokens.PrimaryText, component.Id);
                    ui.TextColored(tokens.SecondaryText, component.Platform + " / " + component.Architecture + "  •  " +
                                                             HumanBytes(component.ArchiveSizeBytes));
                    ui.TextColoredWrapped(component.Healthy ? tokens.Success : tokens.Warning,
                                          component.Healthy ? "Installed and verified"
                                                            : "Repair required: " + component.Diagnostic);
                    if (!component.Healthy)
                    {
                        if (auto disabled = ui.BeginDisabled(mutationBlocked); disabled)
                        {
                            if (HubPrimaryButton(ui, tokens, "Repair from package...", {176.0F, 32.0F}))
                                BrowseForPackage(windows, window, component.Id);
                        }
                        ui.SameLine();
                    }
                    if (auto disabled = ui.BeginDisabled(mutationBlocked); disabled)
                    {
                        if (HubSecondaryButton(ui, tokens, "Remove", {96.0F, 32.0F}))
                        {
                            m_PendingRemoval = component;
                            m_RequestRemoveConfirmation = true;
                        }
                    }
                }
                ui.Spacing();
            }
        }

        void DrawRemovalConfirmation(Keire::UiFrame& ui, const HubDesignTokens& tokens)
        {
            if (!m_PendingRemoval || !m_Selection)
            {
                DrawHubModalHeader(ui, tokens, "Component unavailable",
                                   "The Build Support selection is no longer available.", "EDITOR COMPONENTS");
                if (HubSecondaryButton(ui, tokens, "Close", {96.0F, 36.0F}))
                    ui.CloseCurrentPopup();
                return;
            }
            DrawHubModalHeader(ui, tokens, "Remove " + m_PendingRemoval->Id + '?',
                               "This removes the " + m_PendingRemoval->Platform + " / " +
                                   m_PendingRemoval->Architecture + " component from Kéire Editor " +
                                   m_Selection->Editor.EngineVersion + ". Projects and editor files are not removed.",
                               "CONFIRM COMPONENT REMOVAL");
            const bool removalBlocked = !m_OperationStoreAvailable || ActiveOperation(m_OperationStore).has_value();
            if (auto disabled = ui.BeginDisabled(removalBlocked); disabled)
            {
                if (HubDangerButton(ui, tokens, "Remove component", {164.0F, 36.0F}))
                {
                    const auto component = std::exchange(m_PendingRemoval, std::nullopt);
                    ui.CloseCurrentPopup();
                    StartRemoval(*component);
                }
            }
            ui.SameLine();
            if (HubSecondaryButton(ui, tokens, "Cancel", {96.0F, 36.0F}))
            {
                m_PendingRemoval.reset();
                ui.CloseCurrentPopup();
            }
        }

        BuildSupportOperationStore m_OperationStore;
        std::vector<BuildSupportEditorTarget> m_Editors;
        std::vector<BuildSupportComponent> m_Inventory;
        std::vector<BuildSupportComponent> m_Filtered;
        std::optional<BuildSupportSelection> m_Selection;
        std::optional<BuildSupportFilter> m_PendingFocus;
        std::optional<BuildSupportComponent> m_PendingRemoval;
        std::optional<BuildSupportCommandPlan> m_Operation;
        std::optional<Keire::Detail::ChildProcess> m_Process;
        std::future<BuildSupportRecoveryProbeResult> m_RecoveryProbe;
        std::future<HubResult<bool>> m_RemovalJournalProbe;
        Keire::Ref<Keire::OpenFileDialogOperation> m_FileDialog;
        Keire::Detail::PlayerBuildStatusDocument m_Status;
        std::optional<std::string> m_RepairComponent;
        HubBuildSupportInventoryWorkflow m_InventoryWorkflow{CreateHubBuildSupportInventoryServices()};
        std::chrono::steady_clock::time_point m_NextRecoveryProbe;
        std::string m_OwnedOperationId;
        std::string m_RemovalReconciliationOperationId;
        std::string m_Message;
        std::string m_OperationStoreFailureMessage;
        std::uint64_t m_AppliedInventoryRevision = 0;
        BuildSupportRemovalInventoryGate m_RemovalInventoryGate;
        bool m_OperationStoreAvailable = true;
        bool m_Error = false;
        bool m_RequestOpen = false;
        bool m_RequestRemoveConfirmation = false;
        bool m_CancelRequested = false;
    };

    HubBuildSupportIntegration::HubBuildSupportIntegration() : m_Impl(std::make_unique<Impl>()) {}
    HubBuildSupportIntegration::~HubBuildSupportIntegration() { m_Impl->Stop(); }

    HubStatus HubBuildSupportIntegration::Refresh() { return m_Impl->Refresh(); }
    void HubBuildSupportIntegration::SynchronizeEditors(HubProductSnapshot& product)
    {
        m_Impl->SynchronizeEditors(product);
    }
    HubStatus HubBuildSupportIntegration::ManageEditor(const std::string_view installationId)
    {
        return m_Impl->ManageEditor(installationId);
    }
    HubStatus HubBuildSupportIntegration::FocusTarget(const std::string_view platform,
                                                      const std::string_view architecture)
    {
        return m_Impl->FocusTarget(platform, architecture);
    }
    HubStatus HubBuildSupportIntegration::ImportPackage(const std::filesystem::path& package)
    {
        return m_Impl->ImportPackage(package);
    }
    bool HubBuildSupportIntegration::OwnsTask(const std::string_view taskId) const noexcept
    {
        return m_Impl->OwnsTask(taskId);
    }
    HubStatus HubBuildSupportIntegration::ExecuteTaskCommand(const HubUiCommand& command)
    {
        return m_Impl->ExecuteTaskCommand(command);
    }
    void HubBuildSupportIntegration::Poll() { m_Impl->Poll(); }
    void HubBuildSupportIntegration::Draw(Keire::UiFrame& ui, Keire::WindowSystem& windows,
                                          const Keire::WindowId window, const HubDesignTokens& tokens,
                                          const bool offlineMode)
    {
        m_Impl->Draw(ui, windows, window, tokens, offlineMode);
    }
    void HubBuildSupportIntegration::Stop() noexcept { m_Impl->Stop(); }
} // namespace KeireHub
