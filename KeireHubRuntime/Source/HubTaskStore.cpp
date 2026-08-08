#include "KeireHubRuntime/HubTaskStore.h"

#include <KeireHubRuntimeInternal/Persistence.h>

#include <algorithm>
#include <array>
#include <ranges>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumTaskStoreBytes = std::size_t{4} * 1024 * 1024;
        constexpr std::size_t MaximumTasks = 512;
        constexpr std::size_t MaximumPackagesPerTask = 128;

        [[nodiscard]] std::string_view ToString(const HubTaskKind value) noexcept
        {
            constexpr std::array names{"download", "install", "verify",    "repair",
                                       "remove",   "import",  "hubUpdate", "createProject"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::string_view ToString(const HubTaskState value) noexcept
        {
            constexpr std::array names{"queued",     "downloading", "paused",      "verifying",
                                       "extracting", "installing",  "configuring", "cancelling",
                                       "completed",  "failed",      "cancelled"};
            return names[static_cast<std::size_t>(value)];
        }

        template <typename Enum, std::size_t Size>
        [[nodiscard]] std::optional<Enum> ParseEnum(const std::string_view value, const std::array<Enum, Size>& values,
                                                    std::string_view (*name)(Enum) noexcept)
        {
            for (const auto candidate : values)
            {
                if (name(candidate) == value)
                    return candidate;
            }
            return std::nullopt;
        }

        [[nodiscard]] HubStatus Validate(const HubTask& task)
        {
            if (task.Kind < HubTaskKind::Download || task.Kind > HubTaskKind::CreateProject ||
                task.State < HubTaskState::Queued || task.State > HubTaskState::Cancelled ||
                !Detail::IsBoundedIdentifier(task.Id) || task.DisplayName.empty() || task.DisplayName.size() > 512 ||
                task.PackageIds.size() > MaximumPackagesPerTask ||
                (task.TargetInstallationId && !Detail::IsBoundedIdentifier(*task.TargetInstallationId)) ||
                task.Progress.Attempt > 1'000'000U || task.Progress.CurrentPackage.size() > 256 ||
                task.Progress.Phase.size() > 256 ||
                (task.Progress.TotalBytes != 0 && task.Progress.BytesTransferred > task.Progress.TotalBytes))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The Hub task contains invalid metadata.",
                                           .AffectedItem = task.Id});
            }
            for (const auto& package : task.PackageIds)
            {
                if (!Detail::IsBoundedIdentifier(package))
                    return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                               .Message = "The Hub task contains an invalid package identity.",
                                               .AffectedItem = task.Id});
            }
            if (task.State == HubTaskState::Failed && !task.Failure)
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "A failed Hub task must include an error.",
                                           .AffectedItem = task.Id});
            if (task.State != HubTaskState::Failed && task.Failure)
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "Only failed Hub tasks may retain an error.",
                                           .AffectedItem = task.Id});
            if (task.Failure &&
                (task.Failure->Message.empty() || task.Failure->Message.size() > 4096 ||
                 task.Failure->AffectedItem.size() > 512 || task.Failure->TechnicalDetails.size() > 8192 ||
                 task.Failure->LogReference.size() > 512))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The Hub task error exceeds its allowed limits.",
                                           .AffectedItem = task.Id});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] Detail::Json SerializeError(const HubError& error)
        {
            return {{"code", ToString(error.Code)},
                    {"message", error.Message},
                    {"retryable", error.Retryable},
                    {"affectedItem", error.AffectedItem},
                    {"technicalDetails", error.TechnicalDetails},
                    {"logReference", error.LogReference}};
        }

        [[nodiscard]] Detail::Json Serialize(const std::vector<HubTask>& tasks)
        {
            Detail::Json values = Detail::Json::array();
            for (const auto& task : tasks)
            {
                Detail::Json value{{"id", task.Id},
                                   {"kind", ToString(task.Kind)},
                                   {"displayName", task.DisplayName},
                                   {"packageIds", task.PackageIds},
                                   {"state", ToString(task.State)},
                                   {"progress",
                                    {{"bytesTransferred", task.Progress.BytesTransferred},
                                     {"totalBytes", task.Progress.TotalBytes},
                                     {"bytesPerSecond", task.Progress.BytesPerSecond},
                                     {"attempt", task.Progress.Attempt},
                                     {"currentPackage", task.Progress.CurrentPackage},
                                     {"remainingComponents", task.Progress.RemainingComponents},
                                     {"phase", task.Progress.Phase}}},
                                   {"created", task.CreatedUnixSeconds},
                                   {"updated", task.UpdatedUnixSeconds}};
                if (task.TargetInstallationId)
                    value["targetInstallationId"] = *task.TargetInstallationId;
                if (task.WorkerProcessId)
                    value["workerProcessId"] = *task.WorkerProcessId;
                if (task.Failure)
                    value["failure"] = SerializeError(*task.Failure);
                values.push_back(std::move(value));
            }
            return {{"schemaVersion", HubTaskStore::CurrentSchemaVersion}, {"tasks", std::move(values)}};
        }

        [[nodiscard]] HubError ParseError(const Detail::Json& value)
        {
            const auto code = ParseHubErrorCode(value.at("code").get<std::string>());
            if (!code)
                throw std::invalid_argument("Unknown Hub error code.");
            return {.Code = *code,
                    .Message = value.at("message").get<std::string>(),
                    .Retryable = value.value("retryable", false),
                    .AffectedItem = value.value("affectedItem", std::string{}),
                    .TechnicalDetails = value.value("technicalDetails", std::string{}),
                    .LogReference = value.value("logReference", std::string{})};
        }

        [[nodiscard]] HubResult<std::vector<HubTask>> Parse(const Detail::Json& document)
        {
            try
            {
                if (document.at("schemaVersion").get<std::uint32_t>() != HubTaskStore::CurrentSchemaVersion)
                {
                    return HubResult<std::vector<HubTask>>::Failure(
                        {.Code = HubErrorCode::UnsupportedSchema,
                         .Message = "This task journal uses an unsupported schema.",
                         .AffectedItem = "tasks"});
                }
                const auto& values = document.at("tasks");
                if (!values.is_array() || values.size() > MaximumTasks)
                    throw std::invalid_argument("Invalid task collection.");
                constexpr std::array kinds{HubTaskKind::Download,  HubTaskKind::Install,
                                           HubTaskKind::Verify,    HubTaskKind::Repair,
                                           HubTaskKind::Remove,    HubTaskKind::ImportPackage,
                                           HubTaskKind::HubUpdate, HubTaskKind::CreateProject};
                constexpr std::array states{
                    HubTaskState::Queued,      HubTaskState::Downloading, HubTaskState::Paused,
                    HubTaskState::Verifying,   HubTaskState::Extracting,  HubTaskState::Installing,
                    HubTaskState::Configuring, HubTaskState::Cancelling,  HubTaskState::Completed,
                    HubTaskState::Failed,      HubTaskState::Cancelled};
                std::vector<HubTask> result;
                result.reserve(values.size());
                for (const auto& value : values)
                {
                    HubTask task;
                    task.Id = value.at("id").get<std::string>();
                    const auto kind = ParseEnum(value.at("kind").get<std::string>(), kinds,
                                                static_cast<std::string_view (*)(HubTaskKind) noexcept>(&ToString));
                    const auto state = ParseEnum(value.at("state").get<std::string>(), states,
                                                 static_cast<std::string_view (*)(HubTaskState) noexcept>(&ToString));
                    if (!kind || !state)
                        throw std::invalid_argument("Unknown task kind or state.");
                    task.Kind = *kind;
                    task.State = *state;
                    task.DisplayName = value.at("displayName").get<std::string>();
                    task.PackageIds = value.at("packageIds").get<std::vector<std::string>>();
                    if (value.contains("targetInstallationId"))
                        task.TargetInstallationId = value.at("targetInstallationId").get<std::string>();
                    const auto& progress = value.at("progress");
                    task.Progress.BytesTransferred = progress.value("bytesTransferred", 0ULL);
                    task.Progress.TotalBytes = progress.value("totalBytes", 0ULL);
                    task.Progress.BytesPerSecond = progress.value("bytesPerSecond", 0ULL);
                    task.Progress.Attempt = progress.value("attempt", 0U);
                    task.Progress.CurrentPackage = progress.value("currentPackage", std::string{});
                    task.Progress.RemainingComponents = progress.value("remainingComponents", 0U);
                    task.Progress.Phase = progress.value("phase", std::string{});
                    task.CreatedUnixSeconds = value.at("created").get<std::uint64_t>();
                    task.UpdatedUnixSeconds = value.at("updated").get<std::uint64_t>();
                    if (value.contains("workerProcessId"))
                        task.WorkerProcessId = value.at("workerProcessId").get<std::uint64_t>();
                    if (value.contains("failure"))
                        task.Failure = ParseError(value.at("failure"));
                    if (const auto status = Validate(task); !status)
                        throw std::invalid_argument(status.Error().Message);
                    if (std::ranges::find(result, task.Id, &HubTask::Id) != result.end())
                        throw std::invalid_argument("Duplicate task identity.");
                    result.push_back(std::move(task));
                }
                return HubResult<std::vector<HubTask>>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<std::vector<HubTask>>::Failure({.Code = HubErrorCode::InvalidData,
                                                                 .Message = "The task journal is malformed.",
                                                                 .AffectedItem = "tasks",
                                                                 .TechnicalDetails = error.what()});
            }
        }
    } // namespace

    bool IsTerminal(const HubTaskState state) noexcept
    {
        return state == HubTaskState::Completed || state == HubTaskState::Failed || state == HubTaskState::Cancelled;
    }

    bool IsValidTaskTransition(const HubTaskState from, const HubTaskState to) noexcept
    {
        if (from == to)
            return false;
        switch (from)
        {
        case HubTaskState::Queued:
            return to == HubTaskState::Downloading || to == HubTaskState::Verifying || to == HubTaskState::Extracting ||
                   to == HubTaskState::Installing || to == HubTaskState::Cancelling || to == HubTaskState::Cancelled ||
                   to == HubTaskState::Failed;
        case HubTaskState::Downloading:
            return to == HubTaskState::Paused || to == HubTaskState::Verifying || to == HubTaskState::Cancelling ||
                   to == HubTaskState::Queued || to == HubTaskState::Failed;
        case HubTaskState::Paused:
            return to == HubTaskState::Queued || to == HubTaskState::Downloading || to == HubTaskState::Cancelling ||
                   to == HubTaskState::Cancelled;
        case HubTaskState::Verifying:
            return to == HubTaskState::Extracting || to == HubTaskState::Installing || to == HubTaskState::Completed ||
                   to == HubTaskState::Cancelling || to == HubTaskState::Failed;
        case HubTaskState::Extracting:
            return to == HubTaskState::Installing || to == HubTaskState::Cancelling || to == HubTaskState::Failed;
        case HubTaskState::Installing:
            return to == HubTaskState::Configuring || to == HubTaskState::Completed || to == HubTaskState::Cancelling ||
                   to == HubTaskState::Failed;
        case HubTaskState::Configuring:
            return to == HubTaskState::Completed || to == HubTaskState::Cancelling || to == HubTaskState::Failed;
        case HubTaskState::Cancelling:
            // Publication may win the narrow race after cancellation was requested. Once the worker reports an
            // atomically published install, completed is the only truthful persisted state.
            return to == HubTaskState::Completed || to == HubTaskState::Cancelled || to == HubTaskState::Failed;
        case HubTaskState::Failed:
        case HubTaskState::Cancelled:
            return to == HubTaskState::Queued;
        case HubTaskState::Completed:
            return false;
        }
        return false;
    }

    HubTaskStore::HubTaskStore(std::filesystem::path storePath)
        : m_Path(std::move(storePath)), m_Snapshot(std::make_shared<const std::vector<HubTask>>())
    {
    }

    HubStatus HubTaskStore::Load()
    {
        if (!std::filesystem::exists(m_Path))
        {
            m_Snapshot = std::make_shared<const std::vector<HubTask>>();
            return HubStatus::Success();
        }
        auto document = Detail::ReadJsonFile(m_Path, MaximumTaskStoreBytes);
        if (!document)
        {
            if (document.Error().Code == HubErrorCode::InvalidData)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(document.Error());
        }
        auto tasks = Parse(document.Value());
        if (!tasks)
        {
            if (tasks.Error().Code != HubErrorCode::UnsupportedSchema)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(tasks.Error());
        }
        m_Snapshot = std::make_shared<const std::vector<HubTask>>(std::move(tasks).Value());
        return HubStatus::Success();
    }

    HubStatus HubTaskStore::Add(HubTask task)
    {
        if (task.State != HubTaskState::Queued || task.WorkerProcessId || task.Failure ||
            task.Progress.BytesTransferred != 0 || task.Progress.TotalBytes != 0 || task.Progress.BytesPerSecond != 0 ||
            task.Progress.Attempt != 0 || !task.Progress.CurrentPackage.empty() ||
            task.Progress.RemainingComponents != 0 || !task.Progress.Phase.empty())
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "New Hub tasks must start in an unclaimed queue state.",
                                       .AffectedItem = task.Id});
        if (const auto status = Validate(task); !status)
            return status;
        auto tasks = *m_Snapshot;
        if (tasks.size() >= MaximumTasks)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The Hub task history is full.",
                                       .AffectedItem = task.Id});
        if (std::ranges::find(tasks, task.Id, &HubTask::Id) != tasks.end())
            return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                       .Message = "A task with this identity already exists.",
                                       .AffectedItem = task.Id});
        tasks.push_back(std::move(task));
        return Commit(std::move(tasks));
    }

    HubStatus HubTaskStore::Claim(const std::string& taskId, const HubTaskState state,
                                  const std::uint64_t workerProcessId, const std::uint64_t updatedUnixSeconds)
    {
        auto tasks = *m_Snapshot;
        const auto found = std::ranges::find(tasks, taskId, &HubTask::Id);
        if (found == tasks.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The task is no longer available.",
                                       .AffectedItem = taskId});
        if (workerProcessId == 0 || !IsValidTaskTransition(found->State, state) ||
            found->State != HubTaskState::Queued || IsTerminal(state) || state == HubTaskState::Queued ||
            state == HubTaskState::Paused || state == HubTaskState::Cancelling ||
            updatedUnixSeconds < found->UpdatedUnixSeconds)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The task cannot be claimed by this worker.",
                                       .AffectedItem = taskId});
        }
        found->State = state;
        found->WorkerProcessId = workerProcessId;
        found->UpdatedUnixSeconds = updatedUnixSeconds;
        found->Failure.reset();
        if (const auto status = Validate(*found); !status)
            return status;
        return Commit(std::move(tasks));
    }

    HubStatus HubTaskStore::Transition(const std::string& taskId, const HubTaskState state,
                                       const std::uint64_t updatedUnixSeconds, std::optional<HubError> failure)
    {
        auto tasks = *m_Snapshot;
        const auto found = std::ranges::find(tasks, taskId, &HubTask::Id);
        if (found == tasks.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The task is no longer available.",
                                       .AffectedItem = taskId});
        if (!IsValidTaskTransition(found->State, state))
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The task cannot enter the requested state.",
                                       .AffectedItem = taskId});
        if ((state == HubTaskState::Failed) != failure.has_value())
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "Failed task transitions require exactly one error.",
                                       .AffectedItem = taskId});
        if (updatedUnixSeconds < found->UpdatedUnixSeconds)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "Task timestamps cannot move backwards.",
                                       .AffectedItem = taskId});
        found->State = state;
        found->UpdatedUnixSeconds = updatedUnixSeconds;
        found->Failure = std::move(failure);
        if (state == HubTaskState::Queued)
            found->Progress = {};
        if (IsTerminal(state) || state == HubTaskState::Paused || state == HubTaskState::Queued)
            found->WorkerProcessId.reset();
        return Commit(std::move(tasks));
    }

    HubStatus HubTaskStore::UpdateProgress(const std::string& taskId, HubTaskProgress progress,
                                           const std::uint64_t updatedUnixSeconds)
    {
        auto tasks = *m_Snapshot;
        const auto found = std::ranges::find(tasks, taskId, &HubTask::Id);
        if (found == tasks.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The task is no longer available.",
                                       .AffectedItem = taskId});
        if (IsTerminal(found->State) || found->State == HubTaskState::Cancelling)
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "This task can no longer report progress.",
                                       .AffectedItem = taskId});
        if (updatedUnixSeconds < found->UpdatedUnixSeconds || progress.Attempt < found->Progress.Attempt ||
            (progress.TotalBytes != 0 && progress.BytesTransferred > progress.TotalBytes) ||
            (progress.Attempt == found->Progress.Attempt &&
             progress.BytesTransferred < found->Progress.BytesTransferred))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The task reported invalid or regressing progress.",
                                       .AffectedItem = taskId});
        }
        found->Progress = std::move(progress);
        found->UpdatedUnixSeconds = updatedUnixSeconds;
        if (const auto status = Validate(*found); !status)
            return status;
        return Commit(std::move(tasks));
    }

    HubStatus HubTaskStore::SetWorkerProcess(const std::string& taskId, std::optional<std::uint64_t> processId,
                                             const std::uint64_t updatedUnixSeconds)
    {
        auto tasks = *m_Snapshot;
        const auto found = std::ranges::find(tasks, taskId, &HubTask::Id);
        if (found == tasks.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The task is no longer available.",
                                       .AffectedItem = taskId});
        if (IsTerminal(found->State) || updatedUnixSeconds < found->UpdatedUnixSeconds ||
            (processId && *processId == 0))
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The task cannot accept this worker process.",
                                       .AffectedItem = taskId});
        found->WorkerProcessId = processId;
        found->UpdatedUnixSeconds = updatedUnixSeconds;
        return Commit(std::move(tasks));
    }

    HubStatus HubTaskStore::RemoveTerminal(const std::string& taskId)
    {
        auto tasks = *m_Snapshot;
        const auto found = std::ranges::find(tasks, taskId, &HubTask::Id);
        if (found == tasks.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The task is no longer available.",
                                       .AffectedItem = taskId});
        if (!IsTerminal(found->State))
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "An active task cannot be removed from history.",
                                       .AffectedItem = taskId});
        tasks.erase(found);
        return Commit(std::move(tasks));
    }

    std::shared_ptr<const std::vector<HubTask>> HubTaskStore::Snapshot() const noexcept { return m_Snapshot; }

    const std::filesystem::path& HubTaskStore::Path() const noexcept { return m_Path; }

    HubStatus HubTaskStore::Commit(std::vector<HubTask> tasks)
    {
        if (auto status = Detail::WriteJsonFileAtomically(m_Path, Serialize(tasks)); !status)
            return status;
        m_Snapshot = std::make_shared<const std::vector<HubTask>>(std::move(tasks));
        return HubStatus::Success();
    }
} // namespace KeireHub
