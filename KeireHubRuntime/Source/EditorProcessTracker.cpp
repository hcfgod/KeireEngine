#include "KeireHubRuntime/EditorProcessTracker.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumTrackedProcesses = 256;
        constexpr std::size_t MaximumIdentifierBytes = 128;
        constexpr std::size_t MaximumPathBytes = 4096;

        [[nodiscard]] bool ValidIdentifier(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > MaximumIdentifierBytes)
                return false;
            return std::ranges::all_of(value, [](const unsigned char character)
                                       { return character >= 0x21U && character <= 0x7eU; });
        }

        [[nodiscard]] bool ValidRoot(const std::filesystem::path& root)
        {
            if (root.empty() || !root.is_absolute() || root == root.root_path())
                return false;
            try
            {
                return root.generic_u8string().size() <= MaximumPathBytes;
            }
            catch (...)
            {
                return false;
            }
        }
    } // namespace

    EditorProcessTracker::EditorProcessTracker(ProcessProbe processProbe)
        : m_ProcessProbe(std::move(processProbe)),
          m_Snapshot(std::make_shared<const std::vector<TrackedEditorProcess>>())
    {
    }

    HubStatus EditorProcessTracker::Track(TrackedEditorProcess process)
    {
        static_cast<void>(Refresh());
        if (!m_ProcessProbe || process.ProcessId == 0 || process.LaunchedUnixSeconds == 0 ||
            !ValidIdentifier(process.ProjectId) || !ValidIdentifier(process.InstallationId) ||
            !ValidRoot(process.ProjectRoot))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The launched editor process identity is invalid.",
                                       .AffectedItem = process.ProjectId});
        }
        if (m_Processes.size() >= MaximumTrackedProcesses)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidData,
                                       .Message = "The editor process tracker reached its safety limit.",
                                       .AffectedItem = process.ProjectId});
        }
        const auto duplicate = std::ranges::find_if(m_Processes,
                                                    [&](const TrackedEditorProcess& tracked)
                                                    {
                                                        return tracked.ProcessId == process.ProcessId ||
                                                               tracked.ProjectId == process.ProjectId ||
                                                               tracked.ProjectRoot.lexically_normal() ==
                                                                   process.ProjectRoot.lexically_normal();
                                                    });
        if (duplicate != m_Processes.end())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "This project already has a tracked editor process.",
                                       .AffectedItem = process.ProjectId});
        }
        process.ProjectRoot = process.ProjectRoot.lexically_normal();
        m_Processes.push_back(std::move(process));
        Publish();
        return HubStatus::Success();
    }

    bool EditorProcessTracker::Refresh()
    {
        const auto previousSize = m_Processes.size();
        std::erase_if(m_Processes,
                      [&](const TrackedEditorProcess& process)
                      {
                          try
                          {
                              return !m_ProcessProbe || !m_ProcessProbe(process.ProcessId);
                          }
                          catch (...)
                          {
                              return false;
                          }
                      });
        if (m_Processes.size() == previousSize)
            return false;
        Publish();
        return true;
    }

    bool EditorProcessTracker::IsProjectRunning(const std::string_view projectId) const noexcept
    {
        return std::ranges::any_of(m_Processes,
                                   [&](const TrackedEditorProcess& process) { return process.ProjectId == projectId; });
    }

    bool EditorProcessTracker::IsInstallationRunning(const std::string_view installationId) const noexcept
    {
        return std::ranges::any_of(m_Processes, [&](const TrackedEditorProcess& process)
                                   { return process.InstallationId == installationId; });
    }

    std::shared_ptr<const std::vector<TrackedEditorProcess>> EditorProcessTracker::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    void EditorProcessTracker::Publish()
    {
        m_Snapshot = std::make_shared<const std::vector<TrackedEditorProcess>>(m_Processes);
    }
} // namespace KeireHub
