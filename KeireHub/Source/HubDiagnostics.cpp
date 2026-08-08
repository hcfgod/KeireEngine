#include "KeireHub/HubDiagnostics.h"

#include "Keire/BuildInfo.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] bool IsConfinedRelative(const std::filesystem::path& value) noexcept
        {
            return !value.empty() && !value.is_absolute() &&
                   std::ranges::none_of(value, [](const auto& part) { return part == ".."; });
        }

        [[nodiscard]] std::string RedactedPath(const std::filesystem::path& path, const HubSettings& settings,
                                               const std::filesystem::path& preferenceRoot)
        {
            if (path.empty())
                return "<not configured>";
            const std::array roots{
                std::pair{preferenceRoot, std::string_view("$PREFERENCES")},
                std::pair{settings.DefaultProjectLocation, std::string_view("$PROJECTS")},
                std::pair{settings.DefaultEditorRoot, std::string_view("$EDITORS")},
                std::pair{settings.CacheRoot, std::string_view("$CACHE")},
                std::pair{settings.TemporaryRoot, std::string_view("$TEMP")},
            };
            for (const auto& [root, label] : roots)
            {
                if (root.empty())
                    continue;
                const auto relative = path.lexically_normal().lexically_relative(root.lexically_normal());
                if (relative.empty())
                    return std::string(label);
                if (IsConfinedRelative(relative))
                    return std::string(label) + "/" + Keire::Detail::PathToUtf8(relative);
            }
            return "<external>/" + Keire::Detail::PathToUtf8(path.filename());
        }
    } // namespace

    std::string BuildHubDiagnosticReport(const HubProductSnapshot& snapshot, const Keire::BuildInfo& build,
                                         const std::filesystem::path& preferenceRoot)
    {
        std::ostringstream report;
        report << "Kéire Hub diagnostics\n"
               << "Hub: " << snapshot.HubVersion << '\n'
               << "Build: " << build.Configuration << ", " << build.Compiler << '\n'
               << "Host: " << build.Platform << " / " << build.Architecture << '\n'
               << "Network: "
               << (snapshot.Settings.OfflineMode ? "offline"
                   : snapshot.Online             ? "online"
                   : snapshot.Reconnecting       ? "reconnecting"
                                                 : "unavailable")
               << '\n'
               << "Signed catalog: " << (snapshot.CatalogAvailable ? "available" : "unavailable") << '\n'
               << "Hub update: "
               << (snapshot.HubUpdate
                       ? snapshot.HubUpdate->Version + (snapshot.HubUpdate->Required ? " (required)" : " (available)")
                   : snapshot.HubUpdateMessage.empty() ? "none"
                                                       : "catalog error")
               << '\n'
               << "Proxy: "
               << (snapshot.Settings.NetworkProxyMode == ProxyMode::System ? "system" : "custom configured") << '\n'
               << "Development service override: "
               << (snapshot.Settings.DevelopmentServiceUrl ? "configured" : "not configured") << '\n'
               << "Projects: " << snapshot.RecentProjects << " recent, " << snapshot.PinnedProjects << " pinned\n"
               << "Project root: "
               << RedactedPath(snapshot.Settings.DefaultProjectLocation, snapshot.Settings, preferenceRoot) << '\n'
               << "Editor root: "
               << RedactedPath(snapshot.Settings.DefaultEditorRoot, snapshot.Settings, preferenceRoot) << '\n'
               << "Cache root: " << RedactedPath(snapshot.Settings.CacheRoot, snapshot.Settings, preferenceRoot) << '\n'
               << "Temporary root: " << RedactedPath(snapshot.Settings.TemporaryRoot, snapshot.Settings, preferenceRoot)
               << '\n'
               << "Logs: " << RedactedPath(preferenceRoot / "Hub" / "Logs", snapshot.Settings, preferenceRoot) << '\n'
               << "Task journal: "
               << RedactedPath(preferenceRoot / "Hub" / "tasks.json", snapshot.Settings, preferenceRoot) << '\n'
               << "Editors: " << snapshot.Editors.size() << '\n';
        for (const auto& editor : snapshot.Editors)
        {
            report << "  - " << editor.Id << ": " << editor.Version << ", " << editor.Channel << ", "
                   << (editor.Managed ? "managed" : "external") << ", " << editor.HealthLabel << ", "
                   << RedactedPath(editor.Root, snapshot.Settings, preferenceRoot) << '\n';
        }
        const auto activeTasks = std::ranges::count_if(snapshot.Tasks, &HubTaskUiRecord::Active);
        const auto failedTasks =
            std::ranges::count_if(snapshot.Tasks, [](const auto& task) { return task.Phase == "Failed"; });
        report << "Tasks: " << snapshot.Tasks.size() << " total, " << activeTasks << " active, " << failedTasks
               << " failed\n"
               << "Notifications: " << snapshot.Notifications.size() << " total, " << snapshot.UnreadNotifications
               << " unread\n";
        for (const auto& notification : snapshot.Notifications)
            if (notification.Severity == "Error")
                report << "  - recent failure: " << notification.Title << '\n';
        return report.str();
    }

    bool HubLogsAvailable(const std::filesystem::path& preferenceRoot) noexcept
    {
        std::error_code error;
        return std::filesystem::is_directory(preferenceRoot / "Hub" / "Logs", error) && !error;
    }

    HubFatalRecoveryOutcome HandleHubFatalRecoveryAction(const HubFatalUiAction action,
                                                         const HubProductSnapshot& snapshot,
                                                         Keire::WindowSystem& windows,
                                                         const std::filesystem::path& preferenceRoot)
    {
        if (action == HubFatalUiAction::Close)
            return {.CloseRequested = true};
        if (action == HubFatalUiAction::None)
            return {};

        try
        {
            if (action == HubFatalUiAction::OpenLogs)
            {
                std::string diagnostic;
                if (!Keire::Detail::RevealInFileManager(preferenceRoot / "Hub" / "Logs", diagnostic))
                    return {.Message = "The Hub log directory is unavailable.", .TechnicalDetails = diagnostic};
                return {};
            }

            auto report = BuildHubDiagnosticReport(snapshot, Keire::GetBuildInfo(), preferenceRoot);
            report += "Runtime startup: failed\n";
            windows.SetClipboardText(report);
            return {.Message = "Redacted diagnostics copied to the clipboard."};
        }
        catch (const std::exception& error)
        {
            return {.Message = "That recovery action is unavailable. See the Hub log if accessible.",
                    .TechnicalDetails = error.what()};
        }
    }
} // namespace KeireHub
