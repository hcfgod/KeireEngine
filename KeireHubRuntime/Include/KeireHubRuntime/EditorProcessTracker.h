#pragma once

#include "KeireHubRuntime/EditorInstallationManager.h"
#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    struct TrackedEditorProcess final
    {
        std::uint64_t ProcessId = 0;
        std::string ProjectId;
        std::string InstallationId;
        std::filesystem::path ProjectRoot;
        std::filesystem::path Executable;
        std::uint64_t LaunchedUnixSeconds = 0;
        std::uint64_t ProcessIdentity = 0;
    };

    class EditorProcessTracker final
    {
      public:
        using ProcessProbe = std::function<EditorProcessObservation(std::uint64_t, const std::filesystem::path&)>;

        explicit EditorProcessTracker(ProcessProbe processProbe);

        [[nodiscard]] HubStatus Track(TrackedEditorProcess process);
        [[nodiscard]] bool Refresh();
        [[nodiscard]] bool IsProjectRunning(std::string_view projectId) const noexcept;
        [[nodiscard]] bool IsInstallationRunning(std::string_view installationId) const noexcept;
        [[nodiscard]] std::shared_ptr<const std::vector<TrackedEditorProcess>> Snapshot() const noexcept;

      private:
        void Publish();

        ProcessProbe m_ProcessProbe;
        std::vector<TrackedEditorProcess> m_Processes;
        std::shared_ptr<const std::vector<TrackedEditorProcess>> m_Snapshot;
    };
} // namespace KeireHub
