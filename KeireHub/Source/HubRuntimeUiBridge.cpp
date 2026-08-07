#include "KeireHub/HubRuntimeUiBridge.h"

#include "KeireHubRuntime/HubTaskManager.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string_view TaskPhase(const HubTaskState state) noexcept
        {
            switch (state)
            {
            case HubTaskState::Queued:
                return "Queued";
            case HubTaskState::Downloading:
                return "Downloading";
            case HubTaskState::Paused:
                return "Paused";
            case HubTaskState::Verifying:
                return "Verifying";
            case HubTaskState::Extracting:
                return "Extracting";
            case HubTaskState::Installing:
                return "Installing";
            case HubTaskState::Configuring:
                return "Configuring";
            case HubTaskState::Cancelling:
                return "Cancelling";
            case HubTaskState::Completed:
                return "Completed";
            case HubTaskState::Failed:
                return "Failed";
            case HubTaskState::Cancelled:
                return "Cancelled";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string_view NotificationLabel(const NotificationSeverity severity) noexcept
        {
            switch (severity)
            {
            case NotificationSeverity::Info:
                return "Info";
            case NotificationSeverity::Success:
                return "Success";
            case NotificationSeverity::Warning:
                return "Warning";
            case NotificationSeverity::Error:
                return "Error";
            }
            return "Info";
        }

        [[nodiscard]] std::string_view InstallationHealthLabel(const InstallationHealth health) noexcept
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
    } // namespace

    std::uint64_t HubNowUnixSeconds()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    void ApplyRuntimeSnapshot(const HubControllerSnapshot& runtime, HubProductSnapshot& product)
    {
        if (runtime.Settings)
            product.Settings = *runtime.Settings;

        product.Editors.clear();
        if (runtime.Installations)
        {
            product.Editors.reserve(runtime.Installations->size());
            for (const auto& installation : *runtime.Installations)
            {
                product.Editors.push_back(
                    {.Id = installation.Id,
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
                     .Managed = installation.Ownership == InstallationOwnership::Managed,
                     .Healthy = installation.Health == InstallationHealth::Healthy,
                     .HealthLabel = std::string(InstallationHealthLabel(installation.Health))});
            }
        }

        ApplyHubTaskSnapshot(runtime.Tasks ? std::span<const HubTask>(*runtime.Tasks) : std::span<const HubTask>{},
                             product);

        product.Notifications.clear();
        if (runtime.Notifications)
        {
            product.Notifications.reserve(runtime.Notifications->size());
            for (const auto& notification : *runtime.Notifications)
            {
                product.Notifications.push_back({.Id = notification.Id,
                                                 .Severity = std::string(NotificationLabel(notification.Severity)),
                                                 .Title = notification.Title,
                                                 .Message = notification.Message,
                                                 .Read = notification.Read});
            }
        }
        product.UnreadNotifications = runtime.UnreadNotifications;
    }

    void ApplyHubTaskSnapshot(const std::span<const HubTask> tasks, HubProductSnapshot& product)
    {
        product.Tasks.clear();
        product.Tasks.reserve(tasks.size());
        for (const auto& task : tasks)
        {
            const auto progress = task.Progress.TotalBytes == 0
                                      ? 0.0F
                                      : std::clamp(static_cast<float>(task.Progress.BytesTransferred) /
                                                       static_cast<float>(task.Progress.TotalBytes),
                                                   0.0F, 1.0F);
            std::string message = task.Progress.Phase;
            if (task.Failure)
                message = task.Failure->Message;
            product.Tasks.push_back(
                {.Id = task.Id,
                 .Title = task.DisplayName,
                 .Phase = std::string(TaskPhase(task.State)),
                 .Message = std::move(message),
                 .CurrentPackage = task.Progress.CurrentPackage,
                 .Progress = progress,
                 .BytesTransferred = task.Progress.BytesTransferred,
                 .TotalBytes = task.Progress.TotalBytes,
                 .BytesPerSecond = task.Progress.BytesPerSecond,
                 .RemainingComponents = task.Progress.RemainingComponents,
                 .Active = !IsTerminal(task.State),
                 .Pausable = task.State == HubTaskState::Downloading || task.State == HubTaskState::Paused,
                 .Paused = task.State == HubTaskState::Paused,
                 .Cancellable = !IsTerminal(task.State) && task.State != HubTaskState::Cancelling,
                 .Retryable = task.State == HubTaskState::Failed && task.Failure && task.Failure->Retryable});
        }
    }

    HubStatus ExecuteRuntimeUiCommand(HubController& controller, const HubUiCommand& command,
                                      const std::uint64_t nowUnixSeconds)
    {
        HubTaskManager tasks(controller.Tasks(),
                             {.MaximumConcurrentDownloads = controller.Settings().Snapshot()->ConcurrentDownloads});
        switch (command.Type)
        {
        case HubUiCommandType::PauseTask:
            return tasks.Pause(command.ItemId, nowUnixSeconds);
        case HubUiCommandType::ResumeTask:
            return tasks.Resume(command.ItemId, nowUnixSeconds);
        case HubUiCommandType::RetryTask:
            return tasks.Retry(command.ItemId, nowUnixSeconds);
        case HubUiCommandType::CancelTask:
            return tasks.RequestCancel(command.ItemId, nowUnixSeconds);
        case HubUiCommandType::MarkNotificationRead:
            return controller.Notifications().MarkRead(command.ItemId);
        case HubUiCommandType::ClearNotifications:
            return controller.Notifications().Clear();
        default:
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The command is not owned by the Hub runtime.",
                                       .AffectedItem = command.ItemId});
        }
    }
} // namespace KeireHub
