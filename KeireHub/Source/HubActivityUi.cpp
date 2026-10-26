#include "KeireHub/HubProductUi.h"

#include "KeireHub/HubModalUi.h"
#include "KeireHubInternal/HubProductFormatting.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::size_t WrappedLineCount(Keire::UiFrame& ui, const std::string_view text,
                                                   const float availableWidth)
        {
            if (text.empty())
            {
                return 0;
            }
            const auto width = std::max(availableWidth, 1.0F);
            return std::max<std::size_t>(
                1, static_cast<std::size_t>(std::ceil(ui.MeasureText(text).Width * 1.08F / width)));
        }

        [[nodiscard]] bool HasTaskActions(const HubTaskUiRecord& task) noexcept
        {
            return task.Pausable || task.Cancellable || task.Retryable || task.Dismissible;
        }

        void DrawTaskActivityCard(Keire::UiFrame& ui, const HubDesignTokens& tokens, const HubTaskUiRecord& task,
                                  HubUiCommand& command)
        {
            const auto details = Detail::TaskActivityDetails(task);
            const bool showMessage = !task.Message.empty() && task.Message != task.Phase;
            const auto contentWidth = std::max(ui.ContentAvailable().Width - 24.0F, 1.0F);
            const auto lineHeight = std::max(ui.MeasureText("Ag").Height, 16.0F);
            const auto titleLines = WrappedLineCount(ui, task.Title, contentWidth);
            const auto detailLines =
                WrappedLineCount(ui, showMessage ? task.Message : std::string_view{}, contentWidth) +
                WrappedLineCount(ui, details, contentWidth);
            const auto extraLines = (titleLines > 1 ? titleLines - 1 : 0) + (detailLines > 1 ? detailLines - 1 : 0);
            const float baseHeight = HasTaskActions(task) ? 146.0F : 112.0F;
            const float height = std::min(baseHeight + static_cast<float>(extraLines) * lineHeight, 238.0F);
            [[maybe_unused]] const auto cardBackground =
                ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, tokens.Elevated);
            if (auto card = ui.BeginChild("TaskActivityCard", {0.0F, height}, true); card)
            {
                ui.TextColoredWrapped(task.Active ? tokens.Accent : tokens.PrimaryText, task.Title);
                const float progress = std::clamp(task.Progress, 0.0F, 1.0F);
                const auto progressRow = ui.CursorPosition();
                const float progressRowWidth = ui.ContentAvailable().Width;
                ui.TextColored(tokens.SecondaryText, task.Phase.empty() ? "Queued" : task.Phase);
                const auto percentage = std::to_string(static_cast<unsigned>(std::round(progress * 100.0F))) + '%';
                const float percentageWidth = ui.MeasureText(percentage).Width;
                ui.SameLine();
                ui.SetCursorPosition(
                    {progressRow.X + std::max(progressRowWidth - percentageWidth, 0.0F), progressRow.Y});
                ui.TextColored(tokens.PrimaryText, percentage);
                ui.ProgressBar(progress, {0.0F, 8.0F}, " ");
                if (showMessage)
                {
                    ui.TextColoredWrapped(tokens.SecondaryText, task.Message);
                }
                if (!details.empty())
                {
                    ui.TextColoredWrapped(tokens.MutedText, details);
                }
                else if (!showMessage && !task.Message.empty())
                {
                    ui.TextColoredWrapped(tokens.MutedText, task.Message);
                }

                bool actionDrawn = false;
                const auto beginAction = [&]()
                {
                    if (actionDrawn)
                    {
                        ui.SameLine();
                    }
                    else
                    {
                        ui.Spacing();
                    }
                    actionDrawn = true;
                };
                if (task.Pausable)
                {
                    beginAction();
                    if (HubSecondaryButton(ui, tokens, task.Paused ? "Resume" : "Pause", {82.0F, 30.0F}))
                    {
                        command = {.Type = task.Paused ? HubUiCommandType::ResumeTask : HubUiCommandType::PauseTask,
                                   .ItemId = task.Id};
                    }
                }
                if (task.Cancellable)
                {
                    beginAction();
                    if (HubSecondaryButton(ui, tokens, "Cancel", {82.0F, 30.0F}))
                    {
                        command = {.Type = HubUiCommandType::CancelTask, .ItemId = task.Id};
                    }
                }
                if (task.Retryable)
                {
                    beginAction();
                    if (HubPrimaryButton(ui, tokens, "Retry", {82.0F, 30.0F}))
                    {
                        command = {.Type = HubUiCommandType::RetryTask, .ItemId = task.Id};
                    }
                }
                if (task.Dismissible)
                {
                    beginAction();
                    if (HubSecondaryButton(ui, tokens, "Dismiss", {82.0F, 30.0F}))
                    {
                        command = {.Type = HubUiCommandType::DismissTask, .ItemId = task.Id};
                    }
                }
            }
        }

    } // namespace

    void HubProductUi::DrawTaskCenter(Keire::UiFrame& ui, const HubProductSnapshot& snapshot, HubUiCommand& command)
    {
        const bool closeRequested = std::exchange(m_CloseTaskCenter, false);
        if (std::exchange(m_TaskCenterOpen, false))
        {
            ui.OpenPopup("HubTaskPopover");
        }
        const auto bounds = ui.ContentRect();
        constexpr float popoverWidth = 460.0F;
        ui.SetNextWindowPosition(
            {std::max(bounds.Maximum.X - popoverWidth - 12.0F, bounds.Minimum.X + 8.0F), bounds.Minimum.Y + 8.0F},
            false);
        ui.SetNextWindowSize({popoverWidth, 420.0F}, false);
        [[maybe_unused]] const auto popoverBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::PopupBackground, m_Tokens.Surface);
        if (auto center = ui.BeginPopup("HubTaskPopover"); center)
        {
            if (closeRequested)
            {
                ui.CloseCurrentPopup();
                return;
            }
            const auto headerRow = ui.CursorPosition();
            const auto headerWidth = ui.ContentAvailable().Width;
            {
                const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                ui.TextColored(m_Tokens.PrimaryText, "Tasks and downloads");
            }
            ui.SetCursorPosition({headerRow.X + std::max(headerWidth - 64.0F, 0.0F), headerRow.Y});
            if (ui.Button("Close##TaskCenter", {64.0F, 26.0F}))
            {
                ui.CloseCurrentPopup();
            }
            ui.TextColored(m_Tokens.SecondaryText, "Downloads, verification, installation, and recent results.");
            ui.Spacing();
            if (std::ranges::any_of(snapshot.Tasks, &HubTaskUiRecord::Dismissible) &&
                ui.Button("Clear finished##TaskCenter", {104.0F, 26.0F}))
            {
                command.Type = HubUiCommandType::ClearFinishedTasks;
            }
            if (snapshot.Tasks.empty())
            {
                ui.TextColored(m_Tokens.SecondaryText, "No current or recent tasks.");
                return;
            }
            for (const auto& task : snapshot.Tasks)
            {
                auto id = ui.PushId(task.Id);
                DrawTaskActivityCard(ui, m_Tokens, task, command);
                ui.Spacing();
            }
        }
    }

    void HubProductUi::DrawNotificationCenter(Keire::UiFrame& ui, const HubProductSnapshot& snapshot,
                                              HubUiCommand& command)
    {
        const bool closeRequested = std::exchange(m_CloseNotificationCenter, false);
        if (std::exchange(m_NotificationCenterOpen, false))
        {
            ui.OpenPopup("HubNotificationPopover");
        }
        const auto bounds = ui.ContentRect();
        constexpr float popoverWidth = 480.0F;
        ui.SetNextWindowPosition(
            {std::max(bounds.Maximum.X - popoverWidth - 12.0F, bounds.Minimum.X + 8.0F), bounds.Minimum.Y + 8.0F},
            false);
        ui.SetNextWindowSize({popoverWidth, 520.0F}, false);
        [[maybe_unused]] const auto popoverBackground =
            ui.PushStyleColor(Keire::UiStyleColorRole::PopupBackground, m_Tokens.Surface);
        if (auto center = ui.BeginPopup("HubNotificationPopover"); center)
        {
            if (closeRequested)
            {
                ui.CloseCurrentPopup();
                return;
            }
            const auto headerRow = ui.CursorPosition();
            const auto headerWidth = ui.ContentAvailable().Width;
            {
                const auto heading = ui.PushFont(Keire::UiFontRole::Heading);
                ui.TextColored(m_Tokens.PrimaryText, "Activity and notifications");
            }
            ui.SetCursorPosition({headerRow.X + std::max(headerWidth - 64.0F, 0.0F), headerRow.Y});
            if (ui.Button("Close##Notifications", {64.0F, 26.0F}))
            {
                ui.CloseCurrentPopup();
            }
            ui.TextColored(m_Tokens.SecondaryText, "Install progress, completed work, warnings, and recovery actions.");
            ui.Spacing();
            if (!snapshot.Notifications.empty() && ui.Button("Clear history##Notifications", {104.0F, 26.0F}))
            {
                command.Type = HubUiCommandType::ClearNotifications;
            }
            const auto activeTasks = std::ranges::count_if(snapshot.Tasks, &HubTaskUiRecord::Active);
            if (activeTasks != 0)
            {
                ui.Spacing();
                ui.TextColored(m_Tokens.PrimaryText, "Active installs and downloads");
                for (const auto& task : snapshot.Tasks)
                {
                    if (!task.Active)
                    {
                        continue;
                    }
                    auto id = ui.PushId("notification-" + task.Id);
                    DrawTaskActivityCard(ui, m_Tokens, task, command);
                    ui.Spacing();
                }
                ui.Separator();
            }
            if (snapshot.Notifications.empty() && activeTasks == 0)
            {
                ui.TextColored(m_Tokens.SecondaryText, "No current activity or notifications.");
                return;
            }
            if (!snapshot.Notifications.empty())
            {
                ui.TextColored(m_Tokens.PrimaryText, "Notification history");
            }
            for (const auto& notification : snapshot.Notifications)
            {
                auto id = ui.PushId(notification.Id);
                const auto color = notification.Severity == "Error"     ? m_Tokens.Danger
                                   : notification.Severity == "Warning" ? m_Tokens.Warning
                                   : notification.Severity == "Success" ? m_Tokens.Success
                                                                        : m_Tokens.Accent;
                [[maybe_unused]] const auto cardBackground =
                    ui.PushStyleColor(Keire::UiStyleColorRole::ChildBackground, m_Tokens.Elevated);
                const auto title = (notification.Read ? "" : "• ") + notification.Severity + "  " + notification.Title;
                const auto contentWidth = std::max(ui.ContentAvailable().Width - 24.0F, 1.0F);
                const auto lineHeight = std::max(ui.MeasureText("Ag").Height, 16.0F);
                const auto textLines = WrappedLineCount(ui, title, contentWidth) +
                                       WrappedLineCount(ui, notification.Message, contentWidth);
                const auto cardHeight = std::clamp(68.0F + static_cast<float>(textLines) * lineHeight, 118.0F, 238.0F);
                if (auto card = ui.BeginChild("NotificationCard", {0.0F, cardHeight}, true); card)
                {
                    ui.TextColoredWrapped(color, title);
                    ui.TextColoredWrapped(m_Tokens.SecondaryText, notification.Message);
                    ui.Spacing();
                    if (!notification.Read && HubSecondaryButton(ui, m_Tokens, "Mark read", {92.0F, 28.0F}))
                    {
                        command = {.Type = HubUiCommandType::MarkNotificationRead, .ItemId = notification.Id};
                    }
                    if (notification.Read && HubSecondaryButton(ui, m_Tokens, "Dismiss", {92.0F, 28.0F}))
                    {
                        command = {.Type = HubUiCommandType::DismissNotification, .ItemId = notification.Id};
                    }
                }
                ui.Spacing();
            }
        }
    }

} // namespace KeireHub
