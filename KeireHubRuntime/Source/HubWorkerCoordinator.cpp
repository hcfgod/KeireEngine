#include "KeireHubRuntime/HubWorkerCoordinator.h"

#include "KeireHubRuntime/HubTaskManager.h"
#include "KeireHubRuntime/HubWorkerProtocol.h"

#include <KeireHubRuntimeInternal/HubWorkerCoordinatorOperations.h>
#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace KeireHub
{
    namespace
    {
        using Detail::Exists;
        using Detail::OperationPaths;
        using Detail::PathsFor;
        using Detail::PrepareManagedEditorRoot;
        using Detail::PrepareOperationDirectory;
        using Detail::PrepareOperationRoot;
        using Detail::ProtocolFailure;
        using Detail::RemoveKnownFile;

        constexpr auto MaximumPollInterval = std::chrono::seconds(5);
        constexpr auto MaximumStartupTimeout = std::chrono::minutes(2);
        constexpr std::size_t MaximumCommandQueue = 1024;
        constexpr std::array WorkerPhases{HubTaskState::Downloading, HubTaskState::Verifying, HubTaskState::Extracting,
                                          HubTaskState::Installing, HubTaskState::Configuring};

        [[nodiscard]] bool IsEditorPackageTask(const HubTaskKind kind) noexcept
        {
            return kind == HubTaskKind::Install || kind == HubTaskKind::Repair;
        }

        [[nodiscard]] bool MatchesEditorTaskKind(const HubWorkerEditorInstallRequest& request,
                                                 const HubTaskKind kind) noexcept
        {
            return (request.Mode == HubWorkerEditorInstallMode::Install && kind == HubTaskKind::Install) ||
                   (request.Mode == HubWorkerEditorInstallMode::Repair && kind == HubTaskKind::Repair);
        }

        enum class ControlAction
        {
            Pause,
            Resume,
            Cancel,
            Retry
        };

        struct QueueCommand final
        {
            CatalogPackageDownloadRequest Request;
            std::optional<HubWorkerEditorInstallRequest> EditorInstall;
            std::optional<HubWorkerEditorRemovalRequest> EditorRemoval;
            HubTaskKind Kind = HubTaskKind::Download;
        };

        struct ControlCommand final
        {
            ControlAction Action = ControlAction::Pause;
            std::string TaskId;
        };

        using CoordinatorCommand = std::variant<QueueCommand, ControlCommand>;

        [[nodiscard]] std::uint64_t DefaultUnixSeconds() noexcept
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count());
        }

        [[nodiscard]] std::chrono::steady_clock::time_point DefaultMonotonic() noexcept
        {
            return std::chrono::steady_clock::now();
        }

        [[nodiscard]] HubStatus ValidateSpecification(const HubWorkerCoordinatorSpecification& specification)
        {
            if (specification.TaskStorePath.empty() || !specification.TaskStorePath.is_absolute() ||
                specification.OperationRoot.empty() || !specification.OperationRoot.is_absolute() ||
                specification.WorkerExecutable.empty() || !specification.WorkerExecutable.is_absolute() ||
                specification.MaximumConcurrentDownloads == 0 || specification.MaximumConcurrentDownloads > 8 ||
                specification.MaximumPendingCommands == 0 ||
                specification.MaximumPendingCommands > MaximumCommandQueue ||
                specification.PollInterval < std::chrono::milliseconds(1) ||
                specification.PollInterval > MaximumPollInterval ||
                specification.WorkerStartupTimeout < std::chrono::milliseconds(100) ||
                specification.WorkerStartupTimeout > MaximumStartupTimeout)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The Hub worker coordinator configuration is invalid.",
                                           .AffectedItem = "worker-coordinator"});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] bool SameError(const std::optional<HubError>& left, const std::optional<HubError>& right)
        {
            if (left.has_value() != right.has_value())
                return false;
            if (!left)
                return true;
            return left->Code == right->Code && left->Message == right->Message &&
                   left->Retryable == right->Retryable && left->AffectedItem == right->AffectedItem &&
                   left->TechnicalDetails == right->TechnicalDetails && left->LogReference == right->LogReference;
        }

        [[nodiscard]] bool SameProgress(const HubTaskProgress& left, const HubTaskProgress& right) noexcept
        {
            return left.BytesTransferred == right.BytesTransferred && left.TotalBytes == right.TotalBytes &&
                   left.BytesPerSecond == right.BytesPerSecond && left.Attempt == right.Attempt &&
                   left.CurrentPackage == right.CurrentPackage &&
                   left.RemainingComponents == right.RemainingComponents && left.Phase == right.Phase;
        }

        [[nodiscard]] std::optional<HubTask> FindTask(const HubTaskStore& store, const std::string_view taskId)
        {
            const auto snapshot = store.Snapshot();
            const auto found = std::ranges::find_if(*snapshot, [&](const HubTask& task)
                                                    { return std::string_view(task.Id) == taskId; });
            if (found == snapshot->end())
                return std::nullopt;
            return *found;
        }

        [[nodiscard]] std::optional<std::size_t> WorkerPhaseIndex(const HubTaskState state) noexcept
        {
            const auto found = std::ranges::find(WorkerPhases, state);
            if (found == WorkerPhases.end())
                return std::nullopt;
            return static_cast<std::size_t>(std::distance(WorkerPhases.begin(), found));
        }

        [[nodiscard]] std::uint64_t StoreTimestamp(const HubTaskStore& store, const std::uint64_t candidate)
        {
            std::uint64_t result = candidate;
            for (const auto& task : *store.Snapshot())
                result = std::max(result, task.UpdatedUnixSeconds);
            return result;
        }

    } // namespace

    class HubWorkerCoordinator::Impl final
    {
      public:
        Impl(HubWorkerCoordinatorSpecification specification, std::unique_ptr<HubWorkerProcessHost> processHost,
             HubWorkerCoordinatorClocks clocks)
            : m_Specification(std::move(specification)), m_ProcessHost(std::move(processHost)),
              m_Clocks(std::move(clocks)),
              m_Snapshot(std::make_shared<const HubWorkerCoordinatorSnapshot>(HubWorkerCoordinatorSnapshot{
                  .Tasks = std::make_shared<const std::vector<HubTask>>(),
                  .VerifiedDownloads = std::make_shared<const std::vector<HubVerifiedPackageDownload>>(),
                  .CompletedEditorInstalls = std::make_shared<const std::vector<HubCompletedEditorInstall>>(),
                  .CompletedEditorRemovals = std::make_shared<const std::vector<HubCompletedEditorRemoval>>()}))
        {
            if (!m_Clocks.UnixSeconds)
                m_Clocks.UnixSeconds = DefaultUnixSeconds;
            if (!m_Clocks.Monotonic)
                m_Clocks.Monotonic = DefaultMonotonic;
        }

        ~Impl() { Stop(); }

        void Start()
        {
            m_Worker = std::jthread([this](const std::stop_token stopToken) { Run(stopToken); });
        }

        void Stop() noexcept
        {
            {
                std::scoped_lock lock(m_Mutex);
                m_AcceptingCommands = false;
            }
            if (!m_Worker.joinable())
                return;
            m_Worker.request_stop();
            m_Wake.notify_all();
            m_Worker.join();
        }

        HubStatus QueuePackageDownload(CatalogPackageDownloadRequest request)
        {
            if (const auto status = Detail::ValidateCatalogDownload(request); !status)
                return status;
            return SubmitQueue(
                {.Request = std::move(request), .EditorInstall = std::nullopt, .Kind = HubTaskKind::Download});
        }

        HubStatus QueueHubUpdate(CatalogPackageDownloadRequest request)
        {
            if (const auto status = Detail::ValidateCatalogDownload(request); !status)
                return status;
            if (request.Package.Kind != PackageKind::HubInstaller)
            {
                return HubStatus::Failure({.Code = HubErrorCode::PackageManifestInvalid,
                                           .Message = "A Hub update task requires a native Hub installer package.",
                                           .AffectedItem = request.Package.Id,
                                           .TechnicalDetails = {},
                                           .LogReference = {}});
            }
            return SubmitQueue(
                {.Request = std::move(request), .EditorInstall = std::nullopt, .Kind = HubTaskKind::HubUpdate});
        }

        HubStatus QueueEditorPackage(CatalogEditorInstallRequest request, HubWorkerRequest workerRequest,
                                     const HubTaskKind kind)
        {
            if (const auto status = Detail::ValidateCatalogDownload(request.Download); !status)
                return status;
            for (const auto& additional : request.AdditionalDownloads)
            {
                if (additional.TaskId != request.Download.TaskId)
                {
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                               .Message = "Every package in an editor plan must use one task identity.",
                                               .AffectedItem = request.Download.TaskId});
                }
                if (const auto status = Detail::ValidateCatalogDownload(additional); !status)
                    return status;
            }
            if (const auto status = ValidateHubWorkerRequest(workerRequest); !status)
                return status;
            return SubmitQueue({.Request = std::move(request.Download),
                                .EditorInstall = std::move(workerRequest.EditorInstall),
                                .Kind = kind});
        }

        HubStatus QueueEditorInstall(CatalogEditorInstallRequest request)
        {
            auto workerRequest = Detail::CreateEditorInstallWorkerRequest(request);
            return QueueEditorPackage(std::move(request), std::move(workerRequest), HubTaskKind::Install);
        }

        HubStatus QueueEditorRepair(CatalogEditorRepairRequest request)
        {
            auto workerRequest = Detail::CreateEditorRepairWorkerRequest(request);
            return QueueEditorPackage(std::move(request.Install), std::move(workerRequest), HubTaskKind::Repair);
        }

        HubStatus QueueEditorRemoval(const CatalogEditorRemovalRequest& request)
        {
            auto workerRequest = Detail::CreateEditorRemovalWorkerRequest(request);
            if (const auto status = ValidateHubWorkerRequest(workerRequest); !status)
                return status;
            CatalogPackageDownloadRequest metadata;
            metadata.TaskId = request.TaskId;
            return SubmitQueue({.Request = std::move(metadata),
                                .EditorRemoval = std::move(workerRequest.EditorRemoval),
                                .Kind = HubTaskKind::Remove});
        }

        HubStatus SubmitQueue(QueueCommand command)
        {
            const auto taskId = command.Request.TaskId;
            std::scoped_lock lock(m_Mutex);
            if (!m_AcceptingCommands)
                return StoppedError(taskId);
            if (m_Commands.size() >= m_Specification.MaximumPendingCommands)
                return QueueFullError(taskId);
            if (m_PendingTaskIds.contains(taskId) ||
                (m_Snapshot->Tasks &&
                 std::ranges::find(*m_Snapshot->Tasks, taskId, &HubTask::Id) != m_Snapshot->Tasks->end()))
            {
                return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                           .Message = "A task with this identity already exists.",
                                           .AffectedItem = taskId});
            }
            const auto targetInstallation = command.EditorInstall ? std::optional(command.EditorInstall->InstallationId)
                                            : command.EditorRemoval
                                                ? std::optional(command.EditorRemoval->InstallationId)
                                                : std::nullopt;
            if (targetInstallation)
            {
                const bool pendingTarget = std::ranges::any_of(
                    m_Commands,
                    [&](const CoordinatorCommand& pending)
                    {
                        const auto* queue = std::get_if<QueueCommand>(&pending);
                        return queue &&
                               ((queue->EditorInstall && queue->EditorInstall->InstallationId == *targetInstallation) ||
                                (queue->EditorRemoval && queue->EditorRemoval->InstallationId == *targetInstallation));
                    });
                const bool activeTarget =
                    m_Snapshot->Tasks &&
                    std::ranges::any_of(
                        *m_Snapshot->Tasks, [&](const HubTask& task)
                        { return !IsTerminal(task.State) && task.TargetInstallationId == targetInstallation; });
                if (pendingTarget || activeTarget)
                {
                    return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                               .Message = "Another task is already changing this editor installation.",
                                               .Retryable = true,
                                               .AffectedItem = *targetInstallation});
                }
            }
            m_PendingTaskIds.insert(taskId);
            m_Commands.emplace_back(std::move(command));
            m_Wake.notify_one();
            return HubStatus::Success();
        }

        HubStatus SubmitControl(const ControlAction action, const std::string& taskId)
        {
            if (!Detail::IsBoundedIdentifier(taskId))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The package task identity is invalid.",
                                           .AffectedItem = taskId});
            }
            std::scoped_lock lock(m_Mutex);
            if (!m_AcceptingCommands)
                return StoppedError(taskId);
            if (m_Commands.size() >= m_Specification.MaximumPendingCommands)
                return QueueFullError(taskId);
            if (m_Snapshot->State == HubWorkerCoordinatorState::Ready && m_Snapshot->Tasks &&
                std::ranges::find(*m_Snapshot->Tasks, taskId, &HubTask::Id) == m_Snapshot->Tasks->end() &&
                !m_PendingTaskIds.contains(taskId))
            {
                return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                           .Message = "The task is no longer available.",
                                           .AffectedItem = taskId});
            }
            m_Commands.emplace_back(ControlCommand{.Action = action, .TaskId = taskId});
            m_Wake.notify_one();
            return HubStatus::Success();
        }

        std::shared_ptr<const HubWorkerCoordinatorSnapshot> Snapshot() const noexcept
        {
            std::scoped_lock lock(m_Mutex);
            return m_Snapshot;
        }

      private:
        struct LaunchState final
        {
            std::chrono::steady_clock::time_point Started;
        };

        [[nodiscard]] static HubStatus StoppedError(const std::string& taskId)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The package task coordinator is not accepting commands.",
                                       .AffectedItem = taskId});
        }

        [[nodiscard]] static HubStatus QueueFullError(const std::string& taskId)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The package task command queue is full.",
                                       .Retryable = true,
                                       .AffectedItem = taskId});
        }

        [[nodiscard]] std::uint64_t Timestamp(const HubTaskStore& store, const std::string& taskId) const
        {
            const auto task = FindTask(store, taskId);
            return std::max(m_Clocks.UnixSeconds(), task ? task->UpdatedUnixSeconds : std::uint64_t{0});
        }

        void Run(const std::stop_token stopToken) noexcept
        {
            try
            {
                if (auto status = PrepareOperationRoot(m_Specification.OperationRoot); !status)
                {
                    FailCoordinator(status.Error());
                    return;
                }

                HubTaskStore store(m_Specification.TaskStorePath);
                if (auto status = store.Load(); !status)
                {
                    FailCoordinator(status.Error());
                    return;
                }
                RestoreCompletedOperations(store);
                HubTaskManager manager(store,
                                       {.MaximumConcurrentDownloads = m_Specification.MaximumConcurrentDownloads});

                bool changed = PollJournals(store, manager);
                const auto beforeReconcile = store.Snapshot();
                if (auto status = manager.ReconcileWorkers(StoreTimestamp(store, m_Clocks.UnixSeconds()),
                                                           [this, &store](const std::uint64_t processId)
                                                           {
                                                               return m_ProcessHost->IsProcessAlive(processId) ||
                                                                      Detail::HasPendingWorkerResult(
                                                                          store.Snapshot(),
                                                                          m_Specification.OperationRoot, processId);
                                                           });
                    !status)
                {
                    RecordFailure(status.Error());
                }
                changed = changed || beforeReconcile != store.Snapshot();
                Publish(HubWorkerCoordinatorState::Ready, store.Snapshot(), true);

                while (!stopToken.stop_requested())
                {
                    const auto before = store.Snapshot();
                    changed = PollJournals(store, manager);
                    changed = Reconcile(store, manager) || changed;
                    changed = CheckLaunchTimeouts(store, manager) || changed;
                    changed = ProcessCommands(store, manager) || changed;
                    changed = Dispatch(store, manager) || changed;
                    changed = changed || before != store.Snapshot();
                    if (changed || m_FailureChanged)
                        Publish(HubWorkerCoordinatorState::Ready, store.Snapshot(), false);

                    std::unique_lock lock(m_Mutex);
                    m_Wake.wait_for(lock, m_Specification.PollInterval,
                                    [&] { return stopToken.stop_requested() || !m_Commands.empty(); });
                }
                Publish(HubWorkerCoordinatorState::Stopped, store.Snapshot(), true);
            }
            catch (const std::exception& error)
            {
                FailCoordinator({.Code = HubErrorCode::WorkerInterrupted,
                                 .Message = "The package task coordinator stopped unexpectedly.",
                                 .Retryable = true,
                                 .AffectedItem = "worker-coordinator",
                                 .TechnicalDetails = error.what()});
            }
        }

        [[nodiscard]] bool ProcessCommands(HubTaskStore& store, HubTaskManager& manager)
        {
            std::vector<CoordinatorCommand> commands;
            {
                std::scoped_lock lock(m_Mutex);
                commands.swap(m_Commands);
            }
            if (commands.empty())
                return false;

            const auto before = store.Snapshot();
            bool failed = false;
            for (auto& command : commands)
            {
                HubStatus status = std::visit(
                    [&](auto& value) -> HubStatus
                    {
                        using Value = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<Value, QueueCommand>)
                        {
                            const auto taskId = value.Request.TaskId;
                            auto result = Queue(store, manager, std::move(value));
                            if (!result)
                            {
                                std::scoped_lock lock(m_Mutex);
                                m_PendingTaskIds.erase(taskId);
                            }
                            return result;
                        }
                        else
                        {
                            return Control(store, manager, value);
                        }
                    },
                    command);
                if (!status)
                {
                    RecordFailure(status.Error());
                    failed = true;
                }
            }
            if (!failed && m_LastFailure)
            {
                m_LastFailure.reset();
                m_FailureChanged = true;
            }
            return before != store.Snapshot();
        }

        [[nodiscard]] HubStatus Queue(HubTaskStore& store, HubTaskManager& manager, QueueCommand command)
        {
            auto& request = command.Request;
            if (!command.EditorRemoval)
                if (const auto status = Detail::ValidateCatalogDownload(request); !status)
                    return status;
            if ((command.EditorInstall &&
                 (command.EditorRemoval || !MatchesEditorTaskKind(*command.EditorInstall, command.Kind))) ||
                (command.EditorRemoval && command.Kind != HubTaskKind::Remove) ||
                (!command.EditorInstall && !command.EditorRemoval && command.Kind != HubTaskKind::Download &&
                 command.Kind != HubTaskKind::HubUpdate) ||
                (command.Kind == HubTaskKind::HubUpdate && request.Package.Kind != PackageKind::HubInstaller))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The package task kind does not match its worker request.",
                                           .AffectedItem = request.TaskId});
            }
            if (FindTask(store, request.TaskId))
            {
                return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                           .Message = "A task with this identity already exists.",
                                           .AffectedItem = request.TaskId});
            }
            if (command.EditorInstall && command.EditorInstall->Mode == HubWorkerEditorInstallMode::Install)
            {
                if (auto status = PrepareManagedEditorRoot(command.EditorInstall->AllowedInstallRoot, request.TaskId);
                    !status)
                {
                    return status;
                }
            }
            const auto paths = PathsFor(m_Specification.OperationRoot, request.TaskId);
            if (auto status = PrepareOperationDirectory(m_Specification.OperationRoot, paths); !status)
                return status;
            RemoveKnownFile(paths.Status);
            RemoveKnownFile(paths.Result);
            HubWorkerRequest workerRequest{.TaskId = request.TaskId,
                                           .Download = Detail::CreateWorkerDownloadRequest(request),
                                           .EditorInstall = std::move(command.EditorInstall),
                                           .EditorRemoval = std::move(command.EditorRemoval)};
            if (auto status = WriteHubWorkerRequest(paths.Request, workerRequest); !status)
            {
                return status;
            }
            if (auto status = WriteHubWorkerControl(paths.Control, DownloadControl::Continue); !status)
            {
                RemoveKnownFile(paths.Request);
                return status;
            }

            const auto now = m_Clocks.UnixSeconds();
            std::vector<std::string> packageIds =
                workerRequest.EditorRemoval ? std::vector<std::string>{} : std::vector<std::string>{request.Package.Id};
            std::string displayName = request.Package.DisplayName;
            if (workerRequest.EditorInstall)
            {
                packageIds.clear();
                packageIds.reserve(workerRequest.EditorInstall->PackageSteps.size());
                for (const auto& step : workerRequest.EditorInstall->PackageSteps)
                    packageIds.push_back(step.Package.Id);
                displayName = workerRequest.EditorInstall->Package.DisplayName;
            }
            else if (workerRequest.EditorRemoval)
            {
                displayName = "Kéire Editor";
            }
            auto status = manager.Enqueue(
                {.Id = request.TaskId,
                 .Kind = command.Kind,
                 .DisplayName = (command.Kind == HubTaskKind::Repair      ? "Repair "
                                 : workerRequest.EditorInstall            ? "Install "
                                 : workerRequest.EditorRemoval            ? "Remove "
                                 : command.Kind == HubTaskKind::HubUpdate ? "Update "
                                                                          : "Download ") +
                                displayName,
                 .PackageIds = std::move(packageIds),
                 .TargetInstallationId =
                     workerRequest.EditorInstall   ? std::optional(workerRequest.EditorInstall->InstallationId)
                     : workerRequest.EditorRemoval ? std::optional(workerRequest.EditorRemoval->InstallationId)
                                                   : std::nullopt,
                 .State = HubTaskState::Queued,
                 .CreatedUnixSeconds = now,
                 .UpdatedUnixSeconds = now});
            if (!status)
            {
                RemoveKnownFile(paths.Request);
                RemoveKnownFile(paths.Control);
                std::error_code ignored;
                std::filesystem::remove(paths.Directory, ignored);
            }
            return status;
        }

        [[nodiscard]] HubStatus Control(HubTaskStore& store, HubTaskManager& manager, const ControlCommand& command)
        {
            const auto task = FindTask(store, command.TaskId);
            if (!task)
                return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                           .Message = "The task is no longer available.",
                                           .AffectedItem = command.TaskId});
            const auto paths = PathsFor(m_Specification.OperationRoot, command.TaskId);
            if (auto status = PrepareOperationDirectory(m_Specification.OperationRoot, paths); !status)
                return status;
            const auto now = Timestamp(store, command.TaskId);
            switch (command.Action)
            {
            case ControlAction::Pause:
                if (task->State != HubTaskState::Downloading)
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                               .Message = "Only an active download can be paused.",
                                               .AffectedItem = command.TaskId});
                return WriteHubWorkerControl(paths.Control, DownloadControl::Pause);
            case ControlAction::Resume:
                if (task->State != HubTaskState::Paused)
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                               .Message = "Only a paused download can be resumed.",
                                               .AffectedItem = command.TaskId});
                if (m_Launching.contains(command.TaskId))
                    return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                               .Message = "The previous package worker is still stopping.",
                                               .Retryable = true,
                                               .AffectedItem = command.TaskId});
                if (auto status = ResetForDispatch(paths); !status)
                    return status;
                return manager.Resume(command.TaskId, now);
            case ControlAction::Cancel:
                if (IsTerminal(task->State))
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                               .Message = "This task has already finished.",
                                               .AffectedItem = command.TaskId});
                if (auto status = WriteHubWorkerControl(paths.Control, DownloadControl::Cancel); !status)
                    return status;
                return manager.RequestCancel(command.TaskId, now);
            case ControlAction::Retry:
                if (task->State != HubTaskState::Failed)
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                               .Message = "Only a failed task can be retried.",
                                               .AffectedItem = command.TaskId});
                if (!task->Failure || !task->Failure->Retryable)
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                               .Message = "This task cannot be retried.",
                                               .AffectedItem = command.TaskId});
                if (m_Launching.contains(command.TaskId))
                    return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                               .Message = "The previous package worker is still stopping.",
                                               .Retryable = true,
                                               .AffectedItem = command.TaskId});
                if (auto status = ResetForDispatch(paths); !status)
                    return status;
                return manager.Retry(command.TaskId, now);
            }
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The package task command is invalid.",
                                       .AffectedItem = command.TaskId});
        }

        [[nodiscard]] HubStatus ResetForDispatch(const OperationPaths& paths)
        {
            auto request = ReadHubWorkerRequest(paths.Request);
            if (!request)
                return HubStatus::Failure(request.Error());
            RemoveKnownFile(paths.Status);
            RemoveKnownFile(paths.Result);
            return WriteHubWorkerControl(paths.Control, DownloadControl::Continue);
        }

        [[nodiscard]] bool PollJournals(HubTaskStore& store, HubTaskManager& manager)
        {
            const auto before = store.Snapshot();
            for (const auto& task : *before)
            {
                const auto paths = PathsFor(m_Specification.OperationRoot, task.Id);
                if (IsTerminal(task.State))
                {
                    if (Exists(paths.Result))
                        m_Launching.erase(task.Id);
                    continue;
                }
                if (auto prepared = PrepareOperationDirectory(m_Specification.OperationRoot, paths); !prepared)
                {
                    if (auto failed = manager.Fail(task.Id, prepared.Error(), Timestamp(store, task.Id)); !failed)
                        RecordFailure(failed.Error());
                    RecordFailure(prepared.Error());
                    continue;
                }

                std::optional<HubWorkerStatus> workerStatus;
                if (Exists(paths.Status))
                {
                    auto status = ReadHubWorkerStatus(paths.Status);
                    if (!status || status.Value().TaskId != task.Id)
                    {
                        const auto failure =
                            !status ? status.Error() : ProtocolFailure(task.Id, "Worker status task mismatch.");
                        if (auto failed = FailTask(store, manager, task.Id, failure, paths); !failed)
                            RecordFailure(failed.Error());
                        continue;
                    }
                    workerStatus = std::move(status).Value();
                    if (auto statusResult = ApplyStatus(store, manager, *workerStatus); !statusResult)
                        RecordFailure(statusResult.Error());
                }

                if (Exists(paths.Result))
                {
                    auto result = ReadHubWorkerResult(paths.Result);
                    if (!result || result.Value().TaskId != task.Id)
                    {
                        const auto failure =
                            !result ? result.Error() : ProtocolFailure(task.Id, "Worker result task mismatch.");
                        if (auto failed = FailTask(store, manager, task.Id, failure, paths); !failed)
                            RecordFailure(failed.Error());
                        continue;
                    }
                    if (auto status = ApplyResult(store, manager, result.Value(), workerStatus, paths); !status)
                        RecordFailure(status.Error());
                }
            }
            return before != store.Snapshot();
        }

        [[nodiscard]] HubStatus ApplyStatus(HubTaskStore& store, HubTaskManager& manager, const HubWorkerStatus& status)
        {
            auto task = FindTask(store, status.TaskId);
            if (!task || IsTerminal(task->State))
                return HubStatus::Success();
            if (task->WorkerProcessId && *task->WorkerProcessId != status.WorkerProcessId)
                return FailTask(store, manager, task->Id,
                                ProtocolFailure(task->Id, "Another process attempted to replace the task worker."),
                                PathsFor(m_Specification.OperationRoot, task->Id));

            if (task->State == HubTaskState::Queued)
            {
                if (IsTerminal(status.State))
                    return HubStatus::Success();
                const auto ready = manager.Dispatchable();
                const auto expected = std::ranges::find(ready, task->Id, &HubTaskDispatch::TaskId);
                if (expected == ready.end() || expected->InitialState != status.State)
                {
                    return FailTask(store, manager, task->Id,
                                    ProtocolFailure(task->Id, "Worker started in an unexpected task phase."),
                                    PathsFor(m_Specification.OperationRoot, task->Id));
                }
                const HubTaskDispatch& dispatch = *expected;
                if (auto claim = manager.Claim(dispatch, status.WorkerProcessId, Timestamp(store, task->Id)); !claim)
                    return HubStatus::Success();
                m_Launching.erase(task->Id);
                task = FindTask(store, status.TaskId);
            }
            if (!task || task->State == HubTaskState::Cancelling)
                return HubStatus::Success();

            if (status.State == HubTaskState::Paused)
            {
                if (task->State != HubTaskState::Downloading)
                    return HubStatus::Success();
                if (auto progress = ReportProgressIfChanged(store, manager, *task, status.Progress); !progress)
                    return progress;
                return manager.Pause(task->Id, Timestamp(store, task->Id));
            }
            if (IsTerminal(status.State))
                return HubStatus::Success();

            const auto targetPhase = WorkerPhaseIndex(status.State);
            const auto currentPhase = WorkerPhaseIndex(task->State);
            if (!targetPhase || !currentPhase)
            {
                return FailTask(store, manager, task->Id, ProtocolFailure(task->Id, "Unexpected worker task state."),
                                PathsFor(m_Specification.OperationRoot, task->Id));
            }
            const bool phaseAllowed =
                IsEditorPackageTask(task->Kind) ||
                (task->Kind == HubTaskKind::Remove && status.State == HubTaskState::Installing) ||
                ((task->Kind == HubTaskKind::Download || task->Kind == HubTaskKind::HubUpdate) && *targetPhase <= 1);
            if (!phaseAllowed)
            {
                return FailTask(store, manager, task->Id, ProtocolFailure(task->Id, "Unexpected worker task state."),
                                PathsFor(m_Specification.OperationRoot, task->Id));
            }
            if (*targetPhase < *currentPhase)
                return HubStatus::Success();
            for (auto index = *currentPhase + 1; index <= *targetPhase; ++index)
            {
                if (auto advanced = manager.Advance(task->Id, WorkerPhases[index], Timestamp(store, task->Id));
                    !advanced)
                {
                    return advanced;
                }
            }
            task = FindTask(store, status.TaskId);
            if (!task || task->State != status.State)
                return HubStatus::Success();
            return ReportProgressIfChanged(store, manager, *task, status.Progress);
        }

        [[nodiscard]] HubStatus ReportProgressIfChanged(HubTaskStore& store, HubTaskManager& manager,
                                                        const HubTask& task, const HubTaskProgress& progress)
        {
            if (SameProgress(task.Progress, progress))
                return HubStatus::Success();
            if (progress.Attempt < task.Progress.Attempt ||
                (progress.Attempt == task.Progress.Attempt &&
                 progress.BytesTransferred < task.Progress.BytesTransferred))
            {
                return HubStatus::Success();
            }
            return manager.ReportProgress(task.Id, progress, Timestamp(store, task.Id));
        }

        [[nodiscard]] HubStatus ApplyResult(HubTaskStore& store, HubTaskManager& manager, const HubWorkerResult& result,
                                            const std::optional<HubWorkerStatus>& workerStatus,
                                            const OperationPaths& paths)
        {
            auto task = FindTask(store, result.TaskId);
            if (!task || IsTerminal(task->State))
            {
                m_Launching.erase(result.TaskId);
                return HubStatus::Success();
            }
            std::optional<HubWorkerRequest> completedRequest;
            if (result.Outcome == DownloadOutcome::Completed)
            {
                auto request = ReadHubWorkerRequest(paths.Request);
                if (!request)
                    return FailTask(store, manager, task->Id, request.Error(), paths);
                const auto expectedInstall = request.Value().EditorInstall;
                const auto expectedRemoval = request.Value().EditorRemoval;
                if (!expectedRemoval &&
                    result.CachePath.lexically_normal() != DownloadManager::CachePath(request.Value().Download))
                {
                    return FailTask(store, manager, task->Id,
                                    ProtocolFailure(task->Id, "Worker result cache path mismatch."), paths);
                }
                const bool hasInstallResult = !result.InstalledRoot.empty();
                const bool hasRemovalResult = !result.RemovedRoot.empty();
                if (expectedInstall.has_value() != hasInstallResult ||
                    expectedRemoval.has_value() != hasRemovalResult ||
                    (expectedInstall &&
                     (result.InstalledRoot.lexically_normal() != expectedInstall->Destination.lexically_normal() ||
                      result.InstallationId != expectedInstall->InstallationId || !IsEditorPackageTask(task->Kind) ||
                      !MatchesEditorTaskKind(*expectedInstall, task->Kind))) ||
                    (expectedRemoval &&
                     (result.RemovedRoot.lexically_normal() != expectedRemoval->Root.lexically_normal() ||
                      result.InstallationId != expectedRemoval->InstallationId || !result.CachePath.empty() ||
                      task->Kind != HubTaskKind::Remove)) ||
                    (!expectedInstall && !expectedRemoval && task->Kind != HubTaskKind::Download &&
                     task->Kind != HubTaskKind::HubUpdate))
                {
                    return FailTask(store, manager, task->Id,
                                    ProtocolFailure(task->Id, "Worker install result does not match its request."),
                                    paths);
                }
                completedRequest = std::move(request).Value();
            }

            if (task->State == HubTaskState::Queued)
            {
                if (!workerStatus)
                    return FailTask(store, manager, task->Id,
                                    ProtocolFailure(task->Id, "Worker result has no matching status journal."), paths);
                const auto ready = manager.Dispatchable();
                const auto expected = std::ranges::find(ready, task->Id, &HubTaskDispatch::TaskId);
                if (expected == ready.end() ||
                    (expected->InitialState != workerStatus->State && !IsTerminal(workerStatus->State)))
                {
                    return FailTask(store, manager, task->Id,
                                    ProtocolFailure(task->Id, "Worker result started in an unexpected task phase."),
                                    paths);
                }
                const HubTaskDispatch& dispatch = *expected;
                if (auto claim = manager.Claim(dispatch, workerStatus->WorkerProcessId, Timestamp(store, task->Id));
                    !claim)
                {
                    return HubStatus::Success();
                }
                task = FindTask(store, result.TaskId);
            }
            if (!task)
                return HubStatus::Success();

            HubStatus outcome = HubStatus::Success();
            if (task->State == HubTaskState::Cancelling && result.Outcome != DownloadOutcome::Completed)
            {
                outcome = manager.AcknowledgeCancelled(task->Id, Timestamp(store, task->Id));
            }
            else
            {
                switch (result.Outcome)
                {
                case DownloadOutcome::Completed:
                {
                    if (completedRequest && task->State != HubTaskState::Cancelling)
                    {
                        const auto totalBytes = Detail::WorkerRequestDownloadBytes(*completedRequest);
                        const auto attempt = workerStatus
                                                 ? std::max(task->Progress.Attempt, workerStatus->Progress.Attempt)
                                                 : task->Progress.Attempt;
                        outcome = manager.ReportProgress(
                            task->Id,
                            {.BytesTransferred = totalBytes,
                             .TotalBytes = totalBytes,
                             .Attempt = attempt,
                             .CurrentPackage =
                                 completedRequest->EditorInstall   ? completedRequest->EditorInstall->Package.Id
                                 : completedRequest->EditorRemoval ? completedRequest->EditorRemoval->InstallationId
                                                                   : completedRequest->Download.PackageId,
                             .Phase = "Completed"},
                            Timestamp(store, task->Id));
                        if (!outcome)
                            break;
                        task = FindTask(store, result.TaskId);
                    }
                    if (!task)
                        break;
                    if (task->State == HubTaskState::Cancelling)
                    {
                        outcome = manager.Advance(task->Id, HubTaskState::Completed, Timestamp(store, task->Id));
                        break;
                    }

                    const auto targetPhase = completedRequest->EditorInstall   ? HubTaskState::Configuring
                                             : completedRequest->EditorRemoval ? HubTaskState::Installing
                                                                               : HubTaskState::Verifying;
                    const auto currentPhase = WorkerPhaseIndex(task->State);
                    const auto targetPhaseIndex = WorkerPhaseIndex(targetPhase);
                    if (!currentPhase || !targetPhaseIndex || *currentPhase > *targetPhaseIndex)
                    {
                        outcome = HubStatus::Failure(
                            ProtocolFailure(result.TaskId, "Worker completed from an invalid task phase."));
                        break;
                    }
                    for (auto index = *currentPhase + 1; index <= *targetPhaseIndex; ++index)
                    {
                        outcome = manager.Advance(task->Id, WorkerPhases[index], Timestamp(store, task->Id));
                        if (!outcome)
                            break;
                    }
                    if (!outcome)
                        break;
                    outcome = manager.Advance(task->Id, HubTaskState::Completed, Timestamp(store, task->Id));
                    break;
                }
                case DownloadOutcome::Paused:
                    if (task->State == HubTaskState::Downloading)
                        outcome = manager.Pause(task->Id, Timestamp(store, task->Id));
                    else if (task->State != HubTaskState::Paused)
                        outcome = HubStatus::Failure(
                            ProtocolFailure(result.TaskId, "Worker paused from an invalid task phase."));
                    break;
                case DownloadOutcome::Cancelled:
                    outcome = manager.RequestCancel(task->Id, Timestamp(store, task->Id));
                    if (outcome)
                    {
                        task = FindTask(store, result.TaskId);
                        if (task && task->State == HubTaskState::Cancelling)
                            outcome = manager.AcknowledgeCancelled(task->Id, Timestamp(store, task->Id));
                    }
                    break;
                case DownloadOutcome::Failed:
                    outcome = manager.Fail(task->Id, *result.Failure, Timestamp(store, task->Id));
                    break;
                }
            }
            const auto completed = FindTask(store, result.TaskId);
            if (outcome && completed && completed->State == HubTaskState::Completed && completedRequest)
            {
                if (!completedRequest->EditorRemoval)
                {
                    m_VerifiedDownloads[result.TaskId] = {.TaskId = result.TaskId,
                                                          .PackageId = completedRequest->Download.PackageId,
                                                          .Sha256 = completedRequest->Download.Sha256,
                                                          .SizeBytes = completedRequest->Download.SizeBytes,
                                                          .CachePath = result.CachePath};
                }
                if (completedRequest->EditorInstall)
                {
                    m_CompletedEditorInstalls[result.TaskId] =
                        Detail::CreateCompletedEditorInstall(result.TaskId, *completedRequest->EditorInstall);
                }
                else if (completedRequest->EditorRemoval)
                {
                    const auto& removal = *completedRequest->EditorRemoval;
                    m_CompletedEditorRemovals[result.TaskId] =
                        Detail::CreateCompletedEditorRemoval(result.TaskId, removal);
                }
            }
            m_Launching.erase(result.TaskId);
            return outcome;
        }

        void RestoreCompletedOperations(const HubTaskStore& store)
        {
            for (const auto& task : *store.Snapshot())
            {
                if ((task.Kind != HubTaskKind::Download && task.Kind != HubTaskKind::HubUpdate &&
                     !IsEditorPackageTask(task.Kind) && task.Kind != HubTaskKind::Remove) ||
                    task.State != HubTaskState::Completed ||
                    (task.Kind != HubTaskKind::Remove && task.PackageIds.empty()))
                {
                    continue;
                }
                const auto paths = PathsFor(m_Specification.OperationRoot, task.Id);
                if (!Exists(paths.Directory) || !Exists(paths.Request) || !Exists(paths.Result))
                    continue;
                if (auto status = PrepareOperationDirectory(m_Specification.OperationRoot, paths); !status)
                {
                    RecordFailure(status.Error());
                    continue;
                }
                auto request = ReadHubWorkerRequest(paths.Request);
                auto result = ReadHubWorkerResult(paths.Result);
                const auto requestPackageIds =
                    request ? Detail::WorkerRequestPackageIds(request.Value()) : std::vector<std::string>{};
                if (!request || !result || request.Value().TaskId != task.Id || result.Value().TaskId != task.Id ||
                    result.Value().Outcome != DownloadOutcome::Completed || requestPackageIds != task.PackageIds ||
                    (!request.Value().EditorRemoval && result.Value().CachePath.lexically_normal() !=
                                                           DownloadManager::CachePath(request.Value().Download)) ||
                    (request.Value().EditorRemoval && !result.Value().CachePath.empty()) ||
                    (request.Value().EditorInstall.has_value() != IsEditorPackageTask(task.Kind)) ||
                    (request.Value().EditorInstall &&
                     !MatchesEditorTaskKind(*request.Value().EditorInstall, task.Kind)) ||
                    (request.Value().EditorRemoval.has_value() != (task.Kind == HubTaskKind::Remove)) ||
                    (request.Value().EditorInstall &&
                     (result.Value().InstalledRoot.lexically_normal() !=
                          request.Value().EditorInstall->Destination.lexically_normal() ||
                      result.Value().InstallationId != request.Value().EditorInstall->InstallationId)) ||
                    (request.Value().EditorRemoval &&
                     (result.Value().RemovedRoot.lexically_normal() !=
                          request.Value().EditorRemoval->Root.lexically_normal() ||
                      result.Value().InstallationId != request.Value().EditorRemoval->InstallationId)) ||
                    (!request.Value().EditorInstall && !result.Value().InstalledRoot.empty()) ||
                    (!request.Value().EditorRemoval && !result.Value().RemovedRoot.empty()) ||
                    (!request.Value().EditorInstall && !request.Value().EditorRemoval &&
                     !result.Value().InstallationId.empty()))
                {
                    RecordFailure(ProtocolFailure(task.Id, "Completed worker journals do not agree."));
                    continue;
                }
                if (!request.Value().EditorRemoval)
                {
                    m_VerifiedDownloads[task.Id] = {.TaskId = task.Id,
                                                    .PackageId = request.Value().Download.PackageId,
                                                    .Sha256 = request.Value().Download.Sha256,
                                                    .SizeBytes = request.Value().Download.SizeBytes,
                                                    .CachePath = result.Value().CachePath};
                }
                if (request.Value().EditorInstall)
                {
                    m_CompletedEditorInstalls[task.Id] =
                        Detail::CreateCompletedEditorInstall(task.Id, *request.Value().EditorInstall);
                }
                else if (request.Value().EditorRemoval)
                {
                    const auto& removal = *request.Value().EditorRemoval;
                    m_CompletedEditorRemovals[task.Id] = Detail::CreateCompletedEditorRemoval(task.Id, removal);
                }
            }
        }

        [[nodiscard]] HubStatus FailTask(HubTaskStore& store, HubTaskManager& manager, const std::string& taskId,
                                         HubError failure, const OperationPaths& paths)
        {
            const auto task = FindTask(store, taskId);
            if (!task || IsTerminal(task->State))
                return HubStatus::Success();
            (void)WriteHubWorkerControl(paths.Control, DownloadControl::Cancel);
            if (task->State == HubTaskState::Paused)
                return HubStatus::Failure(std::move(failure));
            auto status = manager.Fail(taskId, std::move(failure), Timestamp(store, taskId));
            if (status)
                m_Launching.erase(taskId);
            return status;
        }

        [[nodiscard]] bool Reconcile(HubTaskStore& store, HubTaskManager& manager)
        {
            const auto before = store.Snapshot();
            if (auto status = manager.ReconcileWorkers(StoreTimestamp(store, m_Clocks.UnixSeconds()),
                                                       [this, &store](const std::uint64_t processId)
                                                       {
                                                           return m_ProcessHost->IsProcessAlive(processId) ||
                                                                  Detail::HasPendingWorkerResult(
                                                                      store.Snapshot(), m_Specification.OperationRoot,
                                                                      processId);
                                                       });
                !status)
            {
                RecordFailure(status.Error());
            }
            return before != store.Snapshot();
        }

        [[nodiscard]] bool CheckLaunchTimeouts(HubTaskStore& store, HubTaskManager& manager)
        {
            const auto before = store.Snapshot();
            const auto now = m_Clocks.Monotonic();
            for (auto iterator = m_Launching.begin(); iterator != m_Launching.end();)
            {
                if (now - iterator->second.Started < m_Specification.WorkerStartupTimeout)
                {
                    ++iterator;
                    continue;
                }
                const auto task = FindTask(store, iterator->first);
                if (task && task->State == HubTaskState::Queued)
                {
                    const auto paths = PathsFor(m_Specification.OperationRoot, task->Id);
                    (void)WriteHubWorkerControl(paths.Control, DownloadControl::Cancel);
                    auto status = manager.Fail(
                        task->Id,
                        {.Code = HubErrorCode::WorkerInterrupted,
                         .Message = "The package worker did not report that it started.",
                         .Retryable = true,
                         .AffectedItem = task->Id,
                         .TechnicalDetails = "No valid status journal arrived before the startup timeout."},
                        Timestamp(store, task->Id));
                    if (!status)
                        RecordFailure(status.Error());
                }
                iterator = m_Launching.erase(iterator);
            }
            return before != store.Snapshot();
        }

        [[nodiscard]] bool Dispatch(HubTaskStore& store, HubTaskManager& manager)
        {
            const auto before = store.Snapshot();
            for (const auto& dispatch : manager.Dispatchable())
            {
                if (m_Launching.contains(dispatch.TaskId))
                    continue;
                const auto paths = PathsFor(m_Specification.OperationRoot, dispatch.TaskId);
                if (auto status = PrepareOperationDirectory(m_Specification.OperationRoot, paths); !status)
                {
                    (void)manager.Fail(dispatch.TaskId, status.Error(), Timestamp(store, dispatch.TaskId));
                    RecordFailure(status.Error());
                    continue;
                }
                auto request = ReadHubWorkerRequest(paths.Request);
                const auto task = FindTask(store, dispatch.TaskId);
                const auto requestPackageIds =
                    request ? Detail::WorkerRequestPackageIds(request.Value()) : std::vector<std::string>{};
                if (!request || !task || task->PackageIds != requestPackageIds ||
                    !Detail::WorkerRequestMatchesTaskKind(request.Value(), task->Kind))
                {
                    const auto failure = !request
                                             ? request.Error()
                                             : ProtocolFailure(dispatch.TaskId, "Task and worker request mismatch.");
                    (void)manager.Fail(dispatch.TaskId, failure, Timestamp(store, dispatch.TaskId));
                    RecordFailure(failure);
                    continue;
                }
                RemoveKnownFile(paths.Status);
                RemoveKnownFile(paths.Result);
                if (auto status = WriteHubWorkerControl(paths.Control, DownloadControl::Continue); !status)
                {
                    (void)manager.Fail(dispatch.TaskId, status.Error(), Timestamp(store, dispatch.TaskId));
                    RecordFailure(status.Error());
                    continue;
                }

                HubWorkerLaunch launch{.Executable = m_Specification.WorkerExecutable,
                                       .WorkingDirectory = m_Specification.WorkerExecutable.parent_path(),
                                       .Arguments = {"--request", Detail::PathToUtf8(paths.Request), "--status",
                                                     Detail::PathToUtf8(paths.Status), "--result",
                                                     Detail::PathToUtf8(paths.Result), "--control",
                                                     Detail::PathToUtf8(paths.Control)}};
                if (auto status = m_ProcessHost->LaunchDetached(launch); !status)
                {
                    auto failure = status.Error();
                    failure.Retryable = true;
                    failure.AffectedItem = dispatch.TaskId;
                    (void)manager.Fail(dispatch.TaskId, failure, Timestamp(store, dispatch.TaskId));
                    RecordFailure(std::move(failure));
                    continue;
                }
                m_Launching.emplace(dispatch.TaskId, LaunchState{.Started = m_Clocks.Monotonic()});
            }
            return before != store.Snapshot();
        }

        void RecordFailure(HubError failure)
        {
            if (!SameError(m_LastFailure, std::optional<HubError>(failure)))
            {
                m_LastFailure = std::move(failure);
                m_FailureChanged = true;
            }
        }

        void Publish(const HubWorkerCoordinatorState state, std::shared_ptr<const std::vector<HubTask>> tasks,
                     const bool force)
        {
            std::scoped_lock lock(m_Mutex);
            if (tasks)
            {
                for (const auto& task : *tasks)
                    m_PendingTaskIds.erase(task.Id);
            }
            if (!force && m_Snapshot->State == state && m_Snapshot->Tasks == tasks &&
                SameError(m_Snapshot->LastFailure, m_LastFailure))
            {
                m_FailureChanged = false;
                return;
            }
            std::vector<HubVerifiedPackageDownload> verifiedDownloads;
            verifiedDownloads.reserve(m_VerifiedDownloads.size());
            for (const auto& [taskId, download] : m_VerifiedDownloads)
            {
                (void)taskId;
                verifiedDownloads.push_back(download);
            }
            std::vector<HubCompletedEditorInstall> completedEditorInstalls;
            completedEditorInstalls.reserve(m_CompletedEditorInstalls.size());
            for (const auto& [taskId, install] : m_CompletedEditorInstalls)
            {
                (void)taskId;
                completedEditorInstalls.push_back(install);
            }
            std::vector<HubCompletedEditorRemoval> completedEditorRemovals;
            completedEditorRemovals.reserve(m_CompletedEditorRemovals.size());
            for (const auto& [taskId, removal] : m_CompletedEditorRemovals)
            {
                (void)taskId;
                completedEditorRemovals.push_back(removal);
            }
            m_Snapshot = std::make_shared<const HubWorkerCoordinatorSnapshot>(HubWorkerCoordinatorSnapshot{
                .State = state,
                .Revision = m_Snapshot->Revision + 1,
                .Tasks = std::move(tasks),
                .VerifiedDownloads =
                    std::make_shared<const std::vector<HubVerifiedPackageDownload>>(std::move(verifiedDownloads)),
                .CompletedEditorInstalls =
                    std::make_shared<const std::vector<HubCompletedEditorInstall>>(std::move(completedEditorInstalls)),
                .CompletedEditorRemovals =
                    std::make_shared<const std::vector<HubCompletedEditorRemoval>>(std::move(completedEditorRemovals)),
                .LastFailure = m_LastFailure});
            m_FailureChanged = false;
        }

        void FailCoordinator(HubError failure) noexcept
        {
            try
            {
                RecordFailure(std::move(failure));
                std::shared_ptr<const std::vector<HubTask>> tasks;
                {
                    std::scoped_lock lock(m_Mutex);
                    m_AcceptingCommands = false;
                    tasks = m_Snapshot->Tasks;
                }
                Publish(HubWorkerCoordinatorState::Failed, std::move(tasks), true);
            }
            catch (...)
            {
                std::scoped_lock lock(m_Mutex);
                m_AcceptingCommands = false;
            }
        }

        HubWorkerCoordinatorSpecification m_Specification;
        std::unique_ptr<HubWorkerProcessHost> m_ProcessHost;
        HubWorkerCoordinatorClocks m_Clocks;
        mutable std::mutex m_Mutex;
        std::condition_variable m_Wake;
        std::vector<CoordinatorCommand> m_Commands;
        std::set<std::string, std::less<>> m_PendingTaskIds;
        std::shared_ptr<const HubWorkerCoordinatorSnapshot> m_Snapshot;
        bool m_AcceptingCommands = true;
        std::jthread m_Worker;
        std::map<std::string, LaunchState, std::less<>> m_Launching;
        std::map<std::string, HubVerifiedPackageDownload, std::less<>> m_VerifiedDownloads;
        std::map<std::string, HubCompletedEditorInstall, std::less<>> m_CompletedEditorInstalls;
        std::map<std::string, HubCompletedEditorRemoval, std::less<>> m_CompletedEditorRemovals;
        std::optional<HubError> m_LastFailure;
        bool m_FailureChanged = false;
    };

    HubResult<std::unique_ptr<HubWorkerCoordinator>>
    HubWorkerCoordinator::Create(HubWorkerCoordinatorSpecification specification,
                                 std::unique_ptr<HubWorkerProcessHost> processHost, HubWorkerCoordinatorClocks clocks)
    {
        if (const auto status = ValidateSpecification(specification); !status)
            return HubResult<std::unique_ptr<HubWorkerCoordinator>>::Failure(status.Error());
        if (!processHost)
        {
            return HubResult<std::unique_ptr<HubWorkerCoordinator>>::Failure(
                {.Code = HubErrorCode::InvalidArgument,
                 .Message = "The Hub worker coordinator requires a process host.",
                 .AffectedItem = "worker-coordinator"});
        }
        try
        {
            auto result = std::unique_ptr<HubWorkerCoordinator>(
                new HubWorkerCoordinator(std::move(specification), std::move(processHost), std::move(clocks)));
            result->m_Impl->Start();
            return HubResult<std::unique_ptr<HubWorkerCoordinator>>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<std::unique_ptr<HubWorkerCoordinator>>::Failure(
                {.Code = HubErrorCode::WorkerInterrupted,
                 .Message = "The Hub could not start its package task coordinator.",
                 .Retryable = true,
                 .AffectedItem = "worker-coordinator",
                 .TechnicalDetails = error.what()});
        }
    }

    HubWorkerCoordinator::HubWorkerCoordinator(HubWorkerCoordinatorSpecification specification,
                                               std::unique_ptr<HubWorkerProcessHost> processHost,
                                               HubWorkerCoordinatorClocks clocks)
        : m_Impl(std::make_unique<Impl>(std::move(specification), std::move(processHost), std::move(clocks)))
    {
    }

    HubWorkerCoordinator::~HubWorkerCoordinator() = default;

    HubStatus HubWorkerCoordinator::QueuePackageDownload(CatalogPackageDownloadRequest request)
    {
        return m_Impl->QueuePackageDownload(std::move(request));
    }

    HubStatus HubWorkerCoordinator::QueueHubUpdate(CatalogPackageDownloadRequest request)
    {
        return m_Impl->QueueHubUpdate(std::move(request));
    }

    HubStatus HubWorkerCoordinator::QueueEditorInstall(CatalogEditorInstallRequest request)
    {
        return m_Impl->QueueEditorInstall(std::move(request));
    }

    HubStatus HubWorkerCoordinator::QueueEditorRepair(CatalogEditorRepairRequest request)
    {
        return m_Impl->QueueEditorRepair(std::move(request));
    }

    HubStatus HubWorkerCoordinator::QueueEditorRemoval(const CatalogEditorRemovalRequest& request)
    {
        return m_Impl->QueueEditorRemoval(request);
    }

    HubStatus HubWorkerCoordinator::Pause(const std::string& taskId)
    {
        return m_Impl->SubmitControl(ControlAction::Pause, taskId);
    }

    HubStatus HubWorkerCoordinator::Resume(const std::string& taskId)
    {
        return m_Impl->SubmitControl(ControlAction::Resume, taskId);
    }

    HubStatus HubWorkerCoordinator::Cancel(const std::string& taskId)
    {
        return m_Impl->SubmitControl(ControlAction::Cancel, taskId);
    }

    HubStatus HubWorkerCoordinator::Retry(const std::string& taskId)
    {
        return m_Impl->SubmitControl(ControlAction::Retry, taskId);
    }

    std::shared_ptr<const HubWorkerCoordinatorSnapshot> HubWorkerCoordinator::Snapshot() const noexcept
    {
        return m_Impl->Snapshot();
    }

    void HubWorkerCoordinator::Stop() noexcept { m_Impl->Stop(); }
} // namespace KeireHub
