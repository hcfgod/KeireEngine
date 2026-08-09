#include "KeireHub/HubEditorManagementWorkflow.h"

#include "KeireHubRuntime/LicenseCatalog.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string_view HealthLabel(const InstallationHealth health) noexcept
        {
            switch (health)
            {
            case InstallationHealth::Unknown:
                return "Unknown";
            case InstallationHealth::Healthy:
                return "Verified";
            case InstallationHealth::VerificationRequired:
                return "Verification required";
            case InstallationHealth::Damaged:
                return "Damaged";
            case InstallationHealth::Missing:
                return "Missing";
            }
            return "Unknown";
        }

        [[nodiscard]] HubError ManagementError(const HubErrorCode code, std::string message,
                                               std::string affectedItem = {}, std::string details = {},
                                               const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = std::move(affectedItem),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] HubStatus GuardInactive(const EditorInstallation& installation,
                                              const EditorInstallationActivity activity)
        {
            if (activity.Running)
            {
                return HubStatus::Failure(ManagementError(HubErrorCode::EditorRunning,
                                                          "Close the editor before changing this installation.",
                                                          installation.Id, {}, true));
            }
            if (activity.HasActiveTask)
            {
                return HubStatus::Failure(ManagementError(HubErrorCode::InstallationBusy,
                                                          "Wait for the active installation task to finish.",
                                                          installation.Id, {}, true));
            }
            return HubStatus::Success();
        }

        [[nodiscard]] bool SameInstallationIdentity(const EditorInstallation& left, const EditorInstallation& right)
        {
            return left.Id == right.Id && left.Version == right.Version && left.Channel == right.Channel &&
                   left.Platform == right.Platform && left.Architecture == right.Architecture &&
                   left.Root.lexically_normal() == right.Root.lexically_normal() && left.Ownership == right.Ownership &&
                   left.ManifestFingerprint == right.ManifestFingerprint &&
                   left.PackageTreeIdentity == right.PackageTreeIdentity &&
                   left.PackageReceiptSha256 == right.PackageReceiptSha256 && left.MarkerNonce == right.MarkerNonce &&
                   left.Entrypoints == right.Entrypoints && left.EditorEntrypoint == right.EditorEntrypoint &&
                   left.AssetToolEntrypoint == right.AssetToolEntrypoint &&
                   left.BundledDotnetSdk == right.BundledDotnetSdk &&
                   left.MinimumProjectSchema == right.MinimumProjectSchema &&
                   left.MaximumProjectSchema == right.MaximumProjectSchema &&
                   left.InstalledSizeBytes == right.InstalledSizeBytes;
        }

        [[nodiscard]] EditorInstallationManagerSpecification
        WorkerSpecification(const HubEditorManagementWorkItem& item, std::string hostPlatform,
                            std::string hostArchitecture)
        {
            const auto installationId = item.Installation.Id;
            const auto activity = item.Activity;
            return {.HostPlatform = std::move(hostPlatform),
                    .HostArchitecture = std::move(hostArchitecture),
                    .ProbeActivity = [installationId, activity](const EditorInstallation& installation)
                    {
                        return installation.Id == installationId
                                   ? activity
                                   : EditorInstallationActivity{.Running = true, .HasActiveTask = true};
                    }};
        }

        [[nodiscard]] HubEditorManagementServices DefaultServices()
        {
            HubEditorManagementServices services;
            services.Refresh = [](const std::vector<HubEditorManagementWorkItem>& items,
                                  const std::string& hostPlatform, const std::string& hostArchitecture)
            {
                std::vector<EditorInstallationHealthSnapshot> result;
                result.reserve(items.size());
                for (const auto& item : items)
                {
                    auto inspected = InspectEditorInstallationSnapshot(
                        item.Installation, WorkerSpecification(item, hostPlatform, hostArchitecture));
                    if (!inspected)
                        return HubResult<std::vector<EditorInstallationHealthSnapshot>>::Failure(inspected.Error());
                    result.push_back(std::move(inspected).Value());
                }
                return HubResult<std::vector<EditorInstallationHealthSnapshot>>::Success(std::move(result));
            };
            services.Verify =
                [](const HubEditorManagementWorkItem& item, std::string hostPlatform, std::string hostArchitecture)
            {
                return VerifyEditorInstallationSnapshot(
                    item.Installation, WorkerSpecification(item, std::move(hostPlatform), std::move(hostArchitecture)));
            };
            services.Authorize = [](const HubEditorManagementWorkItem& item, std::filesystem::path expectedRoot,
                                    const EditorManagedOperation operation, std::string hostPlatform,
                                    std::string hostArchitecture)
            {
                const auto specification =
                    WorkerSpecification(item, std::move(hostPlatform), std::move(hostArchitecture));
                auto plan =
                    PrepareManagedEditorOperationSnapshot(item.Installation, expectedRoot, operation, specification);
                if (!plan || operation != EditorManagedOperation::Remove)
                    return plan;
                if (const auto current =
                        RevalidateManagedEditorOperationSnapshot(item.Installation, plan.Value(), specification);
                    !current)
                {
                    return HubResult<EditorManagedOperationPlan>::Failure(current.Error());
                }
                return plan;
            };
            return services;
        }

        [[nodiscard]] HubResult<std::vector<HubLicenseUiRecord>>
        LoadInstalledLicenses(const std::shared_ptr<const std::vector<EditorInstallation>>& installations)
        {
            auto resolved = ResolveInstalledPackageLicenses(*installations);
            if (!resolved)
                return HubResult<std::vector<HubLicenseUiRecord>>::Failure(resolved.Error());
            std::vector<HubLicenseUiRecord> result;
            result.reserve(resolved.Value().size());
            for (auto& license : resolved.Value())
            {
                result.push_back({.Id = std::move(license.Id),
                                  .Name = std::move(license.DisplayName),
                                  .Group = std::move(license.Group),
                                  .Path = std::move(license.SourcePath),
                                  .Text = std::move(license.Text)});
            }
            return HubResult<std::vector<HubLicenseUiRecord>>::Success(std::move(result));
        }

        [[nodiscard]] std::string OperationTitle(const HubEditorManagementOperation operation)
        {
            switch (operation)
            {
            case HubEditorManagementOperation::Refresh:
                return "Refresh editor installations";
            case HubEditorManagementOperation::Verify:
                return "Verify editor installation";
            case HubEditorManagementOperation::AuthorizeRepair:
                return "Prepare managed editor repair";
            case HubEditorManagementOperation::AuthorizeRemoval:
                return "Authorize managed editor removal";
            case HubEditorManagementOperation::None:
                break;
            }
            return "Editor installation maintenance";
        }

        [[nodiscard]] std::string RunningMessage(const HubEditorManagementOperation operation)
        {
            switch (operation)
            {
            case HubEditorManagementOperation::Refresh:
                return "Checking installed editor manifests and file inventories...";
            case HubEditorManagementOperation::Verify:
                return "Verifying the selected editor's declared files...";
            case HubEditorManagementOperation::AuthorizeRepair:
                return "Checking damaged files and managed-install ownership...";
            case HubEditorManagementOperation::AuthorizeRemoval:
                return "Verifying the complete managed editor before removal...";
            case HubEditorManagementOperation::None:
                break;
            }
            return "Checking editor installation state...";
        }
    } // namespace

    std::vector<HubEditorUiRecord>
    BuildHubEditorUiRecords(const std::span<const EditorInstallationHealthSnapshot> installations,
                            const std::span<const HubRecentProject> projects)
    {
        std::vector<HubEditorUiRecord> result;
        result.reserve(installations.size());
        for (const auto& snapshot : installations)
        {
            const auto& installation = snapshot.Installation;
            std::vector<std::string> issues;
            issues.reserve(snapshot.Issues.size());
            for (const auto& issue : snapshot.Issues)
                issues.push_back(issue.Message);
            const auto projectCount =
                std::ranges::count_if(projects,
                                      [&](const auto& project) {
                                          return project.PreferredEditorInstallationId &&
                                                 *project.PreferredEditorInstallationId == installation.Id;
                                      });
            result.push_back({.Id = installation.Id,
                              .Version = installation.Version,
                              .Channel = installation.Channel,
                              .Platform = installation.Platform,
                              .Architecture = installation.Architecture,
                              .Root = installation.Root,
                              .Entrypoint = installation.Root / ResolveEditorEntrypoint(installation),
                              .AssetToolEntrypoint = ResolveAssetToolEntrypoint(installation).empty()
                                                         ? std::filesystem::path{}
                                                         : installation.Root / ResolveAssetToolEntrypoint(installation),
                              .BundledDotnetSdk = installation.BundledDotnetSdk,
                              .MinimumProjectSchema = installation.MinimumProjectSchema,
                              .MaximumProjectSchema = installation.MaximumProjectSchema,
                              .InstalledBytes = installation.InstalledSizeBytes,
                              .ProjectCount = static_cast<std::size_t>(projectCount),
                              .Managed = installation.Ownership == InstallationOwnership::Managed,
                              .Healthy = snapshot.Health == InstallationHealth::Healthy,
                              .Missing = snapshot.Health == InstallationHealth::Missing,
                              .RepairAvailable = installation.Ownership == InstallationOwnership::Managed &&
                                                 snapshot.Health == InstallationHealth::Damaged &&
                                                 !installation.InstalledPackages.empty() &&
                                                 !installation.PackageTreeIdentity.empty() &&
                                                 !installation.PackageReceiptSha256.empty() &&
                                                 !ResolveEditorEntrypoint(installation).empty(),
                              .HealthLabel = std::string(HealthLabel(snapshot.Health)),
                              .HealthIssues = std::move(issues),
                              .Running = snapshot.Activity.Running,
                              .HasActiveTask = snapshot.Activity.HasActiveTask});
        }
        return result;
    }

    HubEditorManagementWorkflow::HubEditorManagementWorkflow(HubController& controller,
                                                             HubEditorManagementSpecification specification,
                                                             HubEditorManagementServices services)
        : m_Controller(controller), m_HostPlatform(std::move(specification.HostPlatform)),
          m_HostArchitecture(std::move(specification.HostArchitecture)),
          m_ProbeRunning(std::move(specification.ProbeRunning)), m_Services(std::move(services)),
          m_OwnerThread(std::this_thread::get_id()),
          m_Snapshot(std::make_shared<const std::vector<EditorInstallationHealthSnapshot>>()),
          m_OperationSnapshot(std::make_shared<const HubEditorManagementOperationSnapshot>())
    {
        auto defaults = DefaultServices();
        if (!m_Services.Refresh)
            m_Services.Refresh = std::move(defaults.Refresh);
        if (!m_Services.Verify)
            m_Services.Verify = std::move(defaults.Verify);
        if (!m_Services.Authorize)
            m_Services.Authorize = std::move(defaults.Authorize);
        ReloadRegistrations();
    }

    HubEditorManagementWorkflow::~HubEditorManagementWorkflow() { JoinWorkers(); }

    HubStatus HubEditorManagementWorkflow::Refresh()
    {
        if (const auto owner = RequireOwnerThread("refresh"); !owner)
            return owner;
        if (m_WorkFuture.valid() || OperationSnapshot()->IsRunning())
        {
            return HubStatus::Failure(ManagementError(HubErrorCode::InvalidTransition,
                                                      "Another editor installation check is already running.", {}, {},
                                                      true));
        }

        m_ActiveRegistrations = m_Controller.Installations().Snapshot();
        m_ActiveTarget.reset();
        std::vector<HubEditorManagementWorkItem> items;
        items.reserve(m_ActiveRegistrations->size());
        for (const auto& installation : *m_ActiveRegistrations)
            items.push_back({.Installation = installation, .Activity = Activity(installation)});

        const auto operationId = m_NextOperationId;
        m_NextOperationId = m_NextOperationId == std::numeric_limits<std::uint64_t>::max() ? 1 : operationId + 1;
        PublishOperation({.OperationId = operationId,
                          .Operation = HubEditorManagementOperation::Refresh,
                          .State = HubEditorManagementState::Running});
        try
        {
            auto refresh = m_Services.Refresh;
            auto hostPlatform = m_HostPlatform;
            auto hostArchitecture = m_HostArchitecture;
            m_WorkFuture = std::async(
                std::launch::async,
                [refresh = std::move(refresh), items = std::move(items), hostPlatform = std::move(hostPlatform),
                 hostArchitecture = std::move(hostArchitecture)]() mutable
                {
                    WorkerResult result;
                    try
                    {
                        auto refreshed =
                            refresh(std::move(items), std::move(hostPlatform), std::move(hostArchitecture));
                        if (refreshed)
                            result.Installations = std::move(refreshed).Value();
                        else
                            result.Failure = refreshed.Error();
                    }
                    catch (const std::exception& error)
                    {
                        result.Failure =
                            ManagementError(HubErrorCode::WorkerInterrupted,
                                            "Editor installations could not be refreshed.", {}, error.what(), true);
                    }
                    catch (...)
                    {
                        result.Failure = ManagementError(
                            HubErrorCode::WorkerInterrupted, "Editor installations could not be refreshed.", {},
                            "The refresh service failed with a non-standard exception.", true);
                    }
                    return result;
                });
        }
        catch (const std::exception& error)
        {
            auto failure = ManagementError(HubErrorCode::WorkerInterrupted,
                                           "The editor refresh worker could not be started.", {}, error.what(), true);
            PublishFailure(failure);
            m_ActiveRegistrations.reset();
            return HubStatus::Failure(std::move(failure));
        }
        return HubStatus::Success();
    }

    void HubEditorManagementWorkflow::ReloadRegistrations()
    {
        if (!RequireOwnerThread("reload registrations"))
            return;
        std::vector<EditorInstallationHealthSnapshot> snapshots;
        const auto installations = m_Controller.Installations().Snapshot();
        snapshots.reserve(installations->size());
        for (const auto& installation : *installations)
        {
            snapshots.push_back(
                {.Installation = installation, .Health = installation.Health, .Activity = Activity(installation)});
        }
        m_Snapshot = std::make_shared<const std::vector<EditorInstallationHealthSnapshot>>(std::move(snapshots));
        QueueLicenseRefresh(installations);
    }

    HubResult<bool> HubEditorManagementWorkflow::Poll()
    {
        if (const auto owner = RequireOwnerThread("poll"); !owner)
            return HubResult<bool>::Failure(owner.Error());
        if (!m_WorkFuture.valid() || m_WorkFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return HubResult<bool>::Success(false);

        WorkerResult result;
        try
        {
            result = m_WorkFuture.get();
        }
        catch (const std::exception& error)
        {
            result.Failure =
                ManagementError(HubErrorCode::WorkerInterrupted,
                                "The editor installation worker did not return a result.", {}, error.what(), true);
        }
        catch (...)
        {
            result.Failure = ManagementError(HubErrorCode::WorkerInterrupted,
                                             "The editor installation worker did not return a result.", {},
                                             "The worker failed with a non-standard exception.", true);
        }

        const auto operation = OperationSnapshot();
        std::optional<InstallationHealth> verifiedHealth;
        if (!result.Failure && m_Controller.Installations().Snapshot() != m_ActiveRegistrations)
        {
            result.Failure = ManagementError(HubErrorCode::InvalidTransition,
                                             "Editor registrations changed while the check was running. Try again.",
                                             operation->InstallationId, {}, true);
        }
        if (!result.Failure && m_ActiveTarget)
        {
            if (const auto current = ValidateCurrentTarget(); !current)
                result.Failure = current.Error();
        }

        if (!result.Failure && operation->Operation == HubEditorManagementOperation::Refresh)
        {
            if (!result.Installations || result.Installations->size() != m_ActiveRegistrations->size())
            {
                result.Failure =
                    ManagementError(HubErrorCode::InvalidData, "The editor refresh returned an incomplete result.");
            }
            else
            {
                for (auto& snapshot : *result.Installations)
                {
                    const auto expected =
                        std::ranges::find(*m_ActiveRegistrations, snapshot.Installation.Id, &EditorInstallation::Id);
                    if (expected == m_ActiveRegistrations->end() ||
                        !SameInstallationIdentity(*expected, snapshot.Installation))
                    {
                        result.Failure = ManagementError(HubErrorCode::InvalidData,
                                                         "The editor refresh returned a mismatched installation.",
                                                         snapshot.Installation.Id);
                        break;
                    }
                    const auto activity = Activity(*expected);
                    snapshot.Installation.Health = snapshot.Health;
                    snapshot.Activity.Running = snapshot.Activity.Running || activity.Running;
                    snapshot.Activity.HasActiveTask = activity.HasActiveTask;
                }
                if (!result.Failure)
                {
                    std::vector<EditorInstallationHealthUpdate> updates;
                    updates.reserve(result.Installations->size());
                    for (const auto& snapshot : *result.Installations)
                        updates.push_back({.InstallationId = snapshot.Installation.Id, .Health = snapshot.Health});
                    if (const auto saved = m_Controller.Installations().UpdateHealth(updates); !saved)
                        result.Failure = saved.Error();
                    else
                    {
                        m_Snapshot = std::make_shared<const std::vector<EditorInstallationHealthSnapshot>>(
                            std::move(*result.Installations));
                        QueueLicenseRefresh(m_Controller.Installations().Snapshot());
                    }
                }
            }
        }
        else if (!result.Failure && operation->Operation == HubEditorManagementOperation::Verify)
        {
            if (!result.Verification || !m_ActiveTarget ||
                !SameInstallationIdentity(result.Verification->Installation, *m_ActiveTarget))
            {
                result.Failure = ManagementError(HubErrorCode::InvalidData,
                                                 "The editor verification returned a mismatched installation.",
                                                 operation->InstallationId);
            }
            else
            {
                auto verified = std::move(*result.Verification);
                verified.Activity = Activity(*m_ActiveTarget);
                verified.Installation.Health = verified.Health;
                verifiedHealth = verified.Health;
                verified.Installation.LastVerifiedUnixSeconds =
                    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                   std::chrono::system_clock::now().time_since_epoch())
                                                   .count());
                if (const auto saved = m_Controller.Installations().Upsert(verified.Installation); !saved)
                    result.Failure = saved.Error();
                else
                {
                    auto snapshots = *m_Snapshot;
                    const auto found = std::ranges::find(snapshots, verified.Installation.Id,
                                                         [](const auto& value) { return value.Installation.Id; });
                    if (found == snapshots.end())
                        snapshots.push_back(std::move(verified));
                    else
                        *found = std::move(verified);
                    m_Snapshot =
                        std::make_shared<const std::vector<EditorInstallationHealthSnapshot>>(std::move(snapshots));
                    QueueLicenseRefresh(m_Controller.Installations().Snapshot());
                }
            }
        }
        else if (!result.Failure && (operation->Operation == HubEditorManagementOperation::AuthorizeRepair ||
                                     operation->Operation == HubEditorManagementOperation::AuthorizeRemoval))
        {
            const auto expectedOperation = operation->Operation == HubEditorManagementOperation::AuthorizeRepair
                                               ? EditorManagedOperation::Repair
                                               : EditorManagedOperation::Remove;
            if (!result.Authorization || !m_ActiveTarget || result.Authorization->Operation != expectedOperation ||
                result.Authorization->InstallationId != m_ActiveTarget->Id ||
                result.Authorization->Root.lexically_normal() != m_ActiveTarget->Root.lexically_normal() ||
                result.Authorization->ManifestFingerprint != m_ActiveTarget->ManifestFingerprint ||
                result.Authorization->PackageTreeIdentity != m_ActiveTarget->PackageTreeIdentity ||
                result.Authorization->PackageReceiptSha256 != m_ActiveTarget->PackageReceiptSha256 ||
                result.Authorization->MarkerNonce != m_ActiveTarget->MarkerNonce)
            {
                result.Failure = ManagementError(HubErrorCode::InvalidData,
                                                 "The editor authorization result did not match its registration.",
                                                 operation->InstallationId);
            }
        }

        HubEditorManagementCompletion completion{.OperationId = operation->OperationId,
                                                 .Operation = operation->Operation,
                                                 .InstallationId = operation->InstallationId,
                                                 .VerifiedHealth = verifiedHealth};
        if (result.Failure)
        {
            completion.Failure = *result.Failure;
            PublishOperation({.OperationId = operation->OperationId,
                              .Operation = operation->Operation,
                              .State = HubEditorManagementState::Failed,
                              .InstallationId = operation->InstallationId,
                              .Failure = result.Failure});
        }
        else
        {
            completion.Authorization = std::move(result.Authorization);
            PublishOperation({.OperationId = operation->OperationId,
                              .Operation = operation->Operation,
                              .State = HubEditorManagementState::Completed,
                              .InstallationId = operation->InstallationId});
        }
        m_Completion = std::move(completion);
        m_ActiveRegistrations.reset();
        m_ActiveTarget.reset();
        return HubResult<bool>::Success(true);
    }

    void HubEditorManagementWorkflow::ApplySnapshot(HubProductSnapshot& product)
    {
        PollLicenses(product);
        const auto runtime = m_Controller.Snapshot();
        const std::span<const HubRecentProject> projects = runtime.Projects
                                                               ? std::span<const HubRecentProject>(*runtime.Projects)
                                                               : std::span<const HubRecentProject>{};
        product.Editors = BuildHubEditorUiRecords(*m_Snapshot, projects);
    }

    void HubEditorManagementWorkflow::ApplyOperationSnapshot(HubProductSnapshot& product) const
    {
        const auto operation = OperationSnapshot();
        product.EditorManagementBusy = operation->IsRunning();
        product.EditorManagementRefreshing =
            operation->IsRunning() && operation->Operation == HubEditorManagementOperation::Refresh;
        for (auto& editor : product.Editors)
        {
            editor.ManagementBusy =
                operation->IsRunning() && (operation->Operation == HubEditorManagementOperation::Refresh ||
                                           editor.Id == operation->InstallationId);
            editor.ManagementStatus = editor.ManagementBusy ? RunningMessage(operation->Operation) : std::string{};
        }
        if (operation->OperationId == 0)
            return;

        const auto taskId = "editor-management-" + std::to_string(operation->OperationId);
        std::erase_if(product.Tasks, [&](const HubTaskUiRecord& task) { return task.Id == taskId; });
        if (!operation->IsRunning())
            return;
        product.Tasks.push_back({.Id = taskId,
                                 .Title = OperationTitle(operation->Operation),
                                 .Phase = "Checking",
                                 .Message = RunningMessage(operation->Operation),
                                 .CurrentPackage = operation->InstallationId,
                                 .Progress = 0.0F,
                                 .Active = true,
                                 .Retryable = false});
    }

    HubStatus HubEditorManagementWorkflow::Execute(const HubUiCommand& command)
    {
        if (const auto owner = RequireOwnerThread("execute"); !owner)
            return owner;
        if (command.Type == HubUiCommandType::RemoveExternalEditor ||
            command.Type == HubUiCommandType::RemoveMissingManagedEditor)
        {
            if (m_WorkFuture.valid() || OperationSnapshot()->IsRunning())
            {
                return HubStatus::Failure(ManagementError(HubErrorCode::InvalidTransition,
                                                          "Wait for the current editor check to finish.",
                                                          command.ItemId, {}, true));
            }
            if (const auto target = ValidateCommandTarget(command); !target)
                return target;
            const auto installations = m_Controller.Installations().Snapshot();
            const auto found = std::ranges::find(*installations, command.ItemId, &EditorInstallation::Id);
            const bool removeMissingManaged = command.Type == HubUiCommandType::RemoveMissingManagedEditor;
            const auto expectedOwnership =
                removeMissingManaged ? InstallationOwnership::Managed : InstallationOwnership::External;
            if (found->Ownership != expectedOwnership)
            {
                return HubStatus::Failure(ManagementError(HubErrorCode::UnsafeInstallRoot,
                                                          removeMissingManaged
                                                              ? "Only a missing managed editor can use this recovery."
                                                              : "Only an external editor registration can be removed.",
                                                          command.ItemId));
            }
            if (const auto inactive = GuardInactive(*found, Activity(*found)); !inactive)
                return inactive;
            auto removed =
                removeMissingManaged
                    ? m_Controller.Installations().RemoveMissingManagedRegistration(command.ItemId, command.Path)
                    : m_Controller.Installations().RemoveExternal(command.ItemId);
            if (!removed)
                return removed;
            ReloadRegistrations();
            return HubStatus::Success();
        }
        if (command.Type == HubUiCommandType::VerifyEditor)
            return StartTargetOperation(command, HubEditorManagementOperation::Verify);
        if (command.Type == HubUiCommandType::RepairManagedEditor)
            return StartTargetOperation(command, HubEditorManagementOperation::AuthorizeRepair);
        if (command.Type == HubUiCommandType::RemoveManagedEditor)
            return StartTargetOperation(command, HubEditorManagementOperation::AuthorizeRemoval);
        return HubStatus::Failure(ManagementError(HubErrorCode::InvalidArgument,
                                                  "The command is not an editor-management command.", command.ItemId));
    }

    std::optional<HubEditorManagementCompletion> HubEditorManagementWorkflow::TakeCompletion()
    {
        if (!RequireOwnerThread("take completion"))
            return std::nullopt;
        return std::exchange(m_Completion, std::nullopt);
    }

    std::shared_ptr<const std::vector<EditorInstallationHealthSnapshot>>
    HubEditorManagementWorkflow::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    std::shared_ptr<const HubEditorManagementOperationSnapshot> HubEditorManagementWorkflow::OperationSnapshot() const
    {
        std::scoped_lock lock(m_OperationMutex);
        return m_OperationSnapshot;
    }

    HubStatus HubEditorManagementWorkflow::RequireOwnerThread(const std::string_view action) const
    {
        if (std::this_thread::get_id() == m_OwnerThread)
            return HubStatus::Success();
        return HubStatus::Failure(ManagementError(HubErrorCode::InvalidTransition,
                                                  "Editor management must be coordinated by its owner thread.",
                                                  std::string(action)));
    }

    HubStatus HubEditorManagementWorkflow::StartTargetOperation(const HubUiCommand& command,
                                                                const HubEditorManagementOperation operation)
    {
        if (m_WorkFuture.valid() || OperationSnapshot()->IsRunning())
        {
            return HubStatus::Failure(ManagementError(HubErrorCode::InvalidTransition,
                                                      "Another editor installation check is already running.",
                                                      command.ItemId, {}, true));
        }
        if (const auto target = ValidateCommandTarget(command); !target)
            return target;
        m_ActiveRegistrations = m_Controller.Installations().Snapshot();
        const auto found = std::ranges::find(*m_ActiveRegistrations, command.ItemId, &EditorInstallation::Id);
        m_ActiveTarget = *found;
        const auto activity = Activity(*m_ActiveTarget);
        if (const auto inactive = GuardInactive(*m_ActiveTarget, activity); !inactive)
        {
            m_ActiveRegistrations.reset();
            m_ActiveTarget.reset();
            return inactive;
        }
        if ((operation == HubEditorManagementOperation::AuthorizeRepair ||
             operation == HubEditorManagementOperation::AuthorizeRemoval) &&
            m_ActiveTarget->Ownership != InstallationOwnership::Managed)
        {
            m_ActiveRegistrations.reset();
            m_ActiveTarget.reset();
            return HubStatus::Failure(ManagementError(HubErrorCode::UnsafeInstallRoot,
                                                      "The editor is not a managed Hub installation.", command.ItemId));
        }

        const auto operationId = m_NextOperationId;
        m_NextOperationId = m_NextOperationId == std::numeric_limits<std::uint64_t>::max() ? 1 : operationId + 1;
        PublishOperation({.OperationId = operationId,
                          .Operation = operation,
                          .State = HubEditorManagementState::Running,
                          .InstallationId = command.ItemId});
        try
        {
            auto verify = m_Services.Verify;
            auto authorize = m_Services.Authorize;
            auto item = HubEditorManagementWorkItem{.Installation = *m_ActiveTarget, .Activity = activity};
            auto installationId = item.Installation.Id;
            auto expectedRoot = command.Path;
            auto hostPlatform = m_HostPlatform;
            auto hostArchitecture = m_HostArchitecture;
            m_WorkFuture = std::async(
                std::launch::async,
                [verify = std::move(verify), authorize = std::move(authorize), item = std::move(item), expectedRoot,
                 installationId = std::move(installationId), hostPlatform = std::move(hostPlatform),
                 hostArchitecture = std::move(hostArchitecture), operation]() mutable
                {
                    WorkerResult result;
                    try
                    {
                        if (operation == HubEditorManagementOperation::Verify)
                        {
                            auto verified =
                                verify(std::move(item), std::move(hostPlatform), std::move(hostArchitecture));
                            if (verified)
                                result.Verification = std::move(verified).Value();
                            else
                                result.Failure = verified.Error();
                        }
                        else
                        {
                            const auto managedOperation = operation == HubEditorManagementOperation::AuthorizeRepair
                                                              ? EditorManagedOperation::Repair
                                                              : EditorManagedOperation::Remove;
                            auto authorization = authorize(std::move(item), std::move(expectedRoot), managedOperation,
                                                           std::move(hostPlatform), std::move(hostArchitecture));
                            if (authorization)
                                result.Authorization = std::move(authorization).Value();
                            else
                                result.Failure = authorization.Error();
                        }
                    }
                    catch (const std::exception& error)
                    {
                        result.Failure = ManagementError(HubErrorCode::WorkerInterrupted,
                                                         "The editor installation check could not be completed.",
                                                         installationId, error.what(), true);
                    }
                    catch (...)
                    {
                        result.Failure = ManagementError(
                            HubErrorCode::WorkerInterrupted, "The editor installation check could not be completed.",
                            installationId, "The service failed with a non-standard exception.", true);
                    }
                    return result;
                });
        }
        catch (const std::exception& error)
        {
            auto failure =
                ManagementError(HubErrorCode::WorkerInterrupted, "The editor installation worker could not be started.",
                                command.ItemId, error.what(), true);
            PublishFailure(failure);
            m_ActiveRegistrations.reset();
            m_ActiveTarget.reset();
            return HubStatus::Failure(std::move(failure));
        }
        return HubStatus::Success();
    }

    HubStatus HubEditorManagementWorkflow::ValidateCommandTarget(const HubUiCommand& command) const
    {
        const auto installations = m_Controller.Installations().Snapshot();
        const auto found = std::ranges::find(*installations, command.ItemId, &EditorInstallation::Id);
        if (found == installations->end())
        {
            return HubStatus::Failure(ManagementError(
                HubErrorCode::NotFound, "The selected editor installation is no longer registered.", command.ItemId));
        }
        if (command.Path.empty() || found->Root.lexically_normal() != command.Path.lexically_normal())
        {
            return HubStatus::Failure(ManagementError(HubErrorCode::UnsafeInstallRoot,
                                                      "The editor command does not target the registered installation.",
                                                      command.ItemId));
        }
        return HubStatus::Success();
    }

    HubStatus HubEditorManagementWorkflow::ValidateCurrentTarget() const
    {
        if (!m_ActiveRegistrations || !m_ActiveTarget)
        {
            return HubStatus::Failure(ManagementError(HubErrorCode::InvalidTransition,
                                                      "The editor installation check lost its captured target."));
        }
        const auto current = m_Controller.Installations().Snapshot();
        if (current != m_ActiveRegistrations)
        {
            return HubStatus::Failure(ManagementError(HubErrorCode::InvalidTransition,
                                                      "The editor registration changed while it was being checked.",
                                                      m_ActiveTarget->Id, {}, true));
        }
        const auto found = std::ranges::find(*current, m_ActiveTarget->Id, &EditorInstallation::Id);
        if (found == current->end() || !SameInstallationIdentity(*found, *m_ActiveTarget))
        {
            return HubStatus::Failure(ManagementError(HubErrorCode::InvalidTransition,
                                                      "The editor identity changed while it was being checked.",
                                                      m_ActiveTarget->Id, {}, true));
        }
        return GuardInactive(*found, Activity(*found));
    }

    EditorInstallationActivity HubEditorManagementWorkflow::Activity(const EditorInstallation& installation) const
    {
        EditorInstallationActivity activity;
        try
        {
            activity.Running = m_ProbeRunning && m_ProbeRunning(installation);
        }
        catch (...)
        {
            activity.Running = true;
        }
        const auto tasks = m_Controller.Tasks().Snapshot();
        activity.HasActiveTask = std::ranges::any_of(*tasks,
                                                     [&](const auto& task) {
                                                         return !IsTerminal(task.State) && task.TargetInstallationId &&
                                                                *task.TargetInstallationId == installation.Id;
                                                     });
        return activity;
    }

    void HubEditorManagementWorkflow::PublishOperation(HubEditorManagementOperationSnapshot snapshot)
    {
        std::scoped_lock lock(m_OperationMutex);
        m_OperationSnapshot = std::make_shared<const HubEditorManagementOperationSnapshot>(std::move(snapshot));
    }

    void HubEditorManagementWorkflow::PublishFailure(HubError error)
    {
        const auto operation = OperationSnapshot();
        m_Completion = HubEditorManagementCompletion{.OperationId = operation->OperationId,
                                                     .Operation = operation->Operation,
                                                     .InstallationId = operation->InstallationId,
                                                     .Failure = error};
        PublishOperation({.OperationId = operation->OperationId,
                          .Operation = operation->Operation,
                          .State = HubEditorManagementState::Failed,
                          .InstallationId = operation->InstallationId,
                          .Failure = std::move(error)});
    }

    void HubEditorManagementWorkflow::QueueLicenseRefresh(
        std::shared_ptr<const std::vector<EditorInstallation>> installations)
    {
        if (installations == m_ObservedInstallations)
            return;
        m_ObservedInstallations = installations;
        m_PendingLicenseInstallations = std::move(installations);
        StartLicenseRefresh();
    }

    void HubEditorManagementWorkflow::StartLicenseRefresh()
    {
        if (m_LicenseFuture.valid() || !m_PendingLicenseInstallations)
            return;
        auto installations = std::exchange(m_PendingLicenseInstallations, {});
        m_LicenseFuture = std::async(std::launch::async, [installations = std::move(installations)]
                                     { return LoadInstalledLicenses(installations); });
    }

    void HubEditorManagementWorkflow::PollLicenses(HubProductSnapshot& product)
    {
        if (m_LicenseFuture.valid() && m_LicenseFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                auto licenses = m_LicenseFuture.get();
                if (licenses)
                {
                    m_Licenses = std::move(licenses).Value();
                    m_LicenseFailure.reset();
                }
                else
                    m_LicenseFailure = licenses.Error();
            }
            catch (const std::exception& error)
            {
                m_LicenseFailure = HubError{.Code = HubErrorCode::IoRead,
                                            .Message = "Installed package licenses could not be loaded.",
                                            .TechnicalDetails = error.what()};
            }
            ++m_LicenseRevision;
            StartLicenseRefresh();
        }

        if (m_AppliedLicenseRevision != m_LicenseRevision)
        {
            std::erase_if(product.Licenses, [](const HubLicenseUiRecord& license)
                          { return license.Id.starts_with("installed-license:"); });
            product.Licenses.insert(product.Licenses.end(), m_Licenses.begin(), m_Licenses.end());
            m_AppliedLicenseRevision = m_LicenseRevision;
        }
        if (m_LicenseFailure)
            product.LicenseMessage = m_LicenseFailure->Message;
        else if (m_LicenseFuture.valid() || m_PendingLicenseInstallations)
            product.LicenseMessage = "Installed package licenses are loading.";
        else
            product.LicenseMessage.clear();
    }

    void HubEditorManagementWorkflow::JoinWorkers() noexcept
    {
        try
        {
            if (m_WorkFuture.valid())
                m_WorkFuture.wait();
            if (m_LicenseFuture.valid())
                m_LicenseFuture.wait();
        }
        catch (...)
        {
        }
    }
} // namespace KeireHub
