#include "KeireHubRuntime/HubController.h"

#include "KeireHubRuntime/HubTaskManager.h"

#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] bool IsWorkerAlive(const std::uint64_t processId) noexcept
        {
            if (processId == 0)
                return false;
#if defined(_WIN32)
            const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
            if (!process)
                return false;
            DWORD exitCode = 0;
            const bool running = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
            CloseHandle(process);
            return running;
#else
            errno = 0;
            return kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
#endif
        }

    } // namespace

    HubController::HubController(HubRuntimePaths paths)
        : m_Settings(paths.PreferenceRoot / "settings.json", std::move(paths.LegacySettingsPath)),
          m_Projects(paths.PreferenceRoot / "projects.json"),
          m_Installations(paths.PreferenceRoot / "installations.json"), m_Tasks(paths.PreferenceRoot / "tasks.json"),
          m_Notifications(paths.PreferenceRoot / "notifications.json"),
          m_Updates(paths.PreferenceRoot / "hub-update.json")
    {
    }

    HubStatus HubController::Load(const std::uint64_t nowUnixSeconds)
    {
        if (auto status = m_Settings.Load(); !status)
            return status;
        if (auto status = m_Projects.Load(); !status)
            return status;
        if (auto status = m_Installations.Load(); !status)
            return status;
        if (auto status = m_Tasks.Load(); !status)
            return status;
        if (auto status = m_Notifications.Load(); !status)
            return status;
        return ReconcileInterruptedTasks(nowUnixSeconds);
    }

    HubStatus HubController::ReconcileInterruptedTasks(const std::uint64_t nowUnixSeconds, WorkerProbe workerProbe)
    {
        if (!workerProbe)
            workerProbe = IsWorkerAlive;
        HubTaskManager manager(m_Tasks, {.MaximumConcurrentDownloads = m_Settings.Snapshot()->ConcurrentDownloads});
        return manager.ReconcileWorkers(nowUnixSeconds, workerProbe);
    }

    HubStatus HubController::UpsertProjectsAndInstallations(const std::span<const HubRecentProject> projects,
                                                            const std::span<const EditorInstallation> installations)
    {
        auto preparedProjects = m_Projects.PrepareUpsertMany(projects);
        if (!preparedProjects)
            return HubStatus::Failure(preparedProjects.Error());
        auto preparedInstallations = m_Installations.PrepareUpsertMany(installations);
        if (!preparedInstallations)
            return HubStatus::Failure(preparedInstallations.Error());
        if (projects.empty() && installations.empty())
            return HubStatus::Success();

        const auto originalProjects = m_Projects.Snapshot();
        std::error_code existsError;
        const bool projectFileExisted = std::filesystem::exists(m_Projects.Path(), existsError);
        if (existsError)
        {
            return HubStatus::Failure({.Code = HubErrorCode::IoRead,
                                       .Message = "The Hub could not prepare its project import transaction.",
                                       .Retryable = true,
                                       .AffectedItem = "projects",
                                       .TechnicalDetails = existsError.message()});
        }

        if (!projects.empty())
        {
            if (auto status = m_Projects.Commit(std::move(preparedProjects).Value()); !status)
                return status;
        }
        if (installations.empty())
            return HubStatus::Success();
        if (auto status = m_Installations.Commit(std::move(preparedInstallations).Value()); !status)
        {
            if (projects.empty())
                return status;
            if (const auto rollback = m_Projects.RestoreSnapshot(originalProjects, projectFileExisted); !rollback)
            {
                return HubStatus::Failure(
                    {.Code = HubErrorCode::IoWrite,
                     .Message = "The Hub could not roll back an incomplete first-run import.",
                     .Retryable = true,
                     .AffectedItem = "first-run-import",
                     .TechnicalDetails = "Editor registry commit failed: " + status.Error().TechnicalDetails +
                                         "; project registry rollback failed: " + rollback.Error().TechnicalDetails});
            }
            return status;
        }
        return HubStatus::Success();
    }

    HubControllerSnapshot HubController::Snapshot() const noexcept
    {
        return {.Settings = m_Settings.Snapshot(),
                .Projects = m_Projects.Snapshot(),
                .Installations = m_Installations.Snapshot(),
                .Tasks = m_Tasks.Snapshot(),
                .Notifications = m_Notifications.Snapshot(),
                .UnreadNotifications = m_Notifications.UnreadCount()};
    }

    HubSettingsStore& HubController::Settings() noexcept { return m_Settings; }

    HubProjectCatalog& HubController::Projects() noexcept { return m_Projects; }

    EditorInstallationRegistry& HubController::Installations() noexcept { return m_Installations; }

    HubTaskStore& HubController::Tasks() noexcept { return m_Tasks; }

    NotificationStore& HubController::Notifications() noexcept { return m_Notifications; }

    HubUpdateManager& HubController::Updates() noexcept { return m_Updates; }
} // namespace KeireHub
