#include "KeireHubRuntime/TaskNotificationTracker.h"

#include <algorithm>
#include <ranges>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string EventId(const HubTask& task, const std::string_view event)
        {
            return task.Id + '-' + std::string(event) + '-' + std::to_string(task.UpdatedUnixSeconds);
        }

        [[nodiscard]] HubNotification Started(const HubTask& task, const std::uint64_t now)
        {
            const auto title = [kind = task.Kind]
            {
                switch (kind)
                {
                case HubTaskKind::Install:
                    return "Editor installation started";
                case HubTaskKind::Remove:
                    return "Editor uninstall started";
                case HubTaskKind::Verify:
                    return "Verification started";
                case HubTaskKind::Repair:
                    return "Editor repair started";
                case HubTaskKind::ImportPackage:
                    return "Package import started";
                case HubTaskKind::HubUpdate:
                    return "Hub update started";
                case HubTaskKind::CreateProject:
                    return "Project creation started";
                case HubTaskKind::Download:
                    return "Download started";
                }
                return "Task started";
            }();
            return {.Id = EventId(task, "started"),
                    .Severity = NotificationSeverity::Info,
                    .Title = title,
                    .Message = task.DisplayName + " is now available in Tasks with live progress and controls.",
                    .CreatedUnixSeconds = now,
                    .RelatedTaskId = task.Id};
        }

        [[nodiscard]] HubNotification Completed(const HubTask& task, const std::uint64_t now)
        {
            const auto title = [kind = task.Kind]
            {
                switch (kind)
                {
                case HubTaskKind::Install:
                    return "Editor installation complete";
                case HubTaskKind::Remove:
                    return "Editor uninstall complete";
                case HubTaskKind::Verify:
                    return "Verification complete";
                case HubTaskKind::Repair:
                    return "Editor repair complete";
                case HubTaskKind::ImportPackage:
                    return "Package import complete";
                case HubTaskKind::HubUpdate:
                    return "Hub update complete";
                case HubTaskKind::CreateProject:
                    return "Project creation complete";
                case HubTaskKind::Download:
                    return "Download complete";
                }
                return "Task complete";
            }();
            return {.Id = EventId(task, "completed"),
                    .Severity = NotificationSeverity::Success,
                    .Title = title,
                    .Message = task.DisplayName + " completed successfully.",
                    .CreatedUnixSeconds = now,
                    .RelatedTaskId = task.Id};
        }

        [[nodiscard]] HubNotification Cancelled(const HubTask& task, const std::uint64_t now)
        {
            return {.Id = EventId(task, "cancelled"),
                    .Severity = NotificationSeverity::Warning,
                    .Title = "Task cancelled",
                    .Message = task.DisplayName + " was cancelled. Verified cache data may be retained for retry.",
                    .CreatedUnixSeconds = now,
                    .RelatedTaskId = task.Id};
        }
    } // namespace

    HubStatus TaskNotificationTracker::Observe(const std::span<const HubTask> tasks, NotificationStore& notifications,
                                               const std::uint64_t nowUnixSeconds)
    {
        if (!m_Initialized)
        {
            for (const auto& task : tasks)
                m_States.insert_or_assign(task.Id, task.State);
            m_Initialized = true;
            return HubStatus::Success();
        }

        for (const auto& task : tasks)
        {
            const auto previous = m_States.find(task.Id);
            if (previous == m_States.end())
            {
                if (!IsTerminal(task.State))
                {
                    if (const auto status = notifications.Add(Started(task, nowUnixSeconds)); !status)
                        return status;
                }
                m_States.insert_or_assign(task.Id, task.State);
                continue;
            }
            if (previous->second == task.State)
                continue;

            if (IsTerminal(previous->second) && !IsTerminal(task.State))
            {
                if (const auto status = notifications.Add(Started(task, nowUnixSeconds)); !status)
                    return status;
            }
            else if (task.State == HubTaskState::Completed)
            {
                if (const auto status = notifications.Add(Completed(task, nowUnixSeconds)); !status)
                    return status;
            }
            else if (task.State == HubTaskState::Cancelled)
            {
                if (const auto status = notifications.Add(Cancelled(task, nowUnixSeconds)); !status)
                    return status;
            }
            previous->second = task.State;
        }

        std::erase_if(m_States, [&](const auto& item)
                      { return std::ranges::find(tasks, item.first, &HubTask::Id) == tasks.end(); });
        return HubStatus::Success();
    }
} // namespace KeireHub
