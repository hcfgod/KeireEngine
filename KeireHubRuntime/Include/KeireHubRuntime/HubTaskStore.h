#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    enum class HubTaskKind
    {
        Download,
        Install,
        Verify,
        Repair,
        Remove,
        ImportPackage,
        HubUpdate,
        CreateProject
    };

    enum class HubTaskState
    {
        Queued,
        Downloading,
        Paused,
        Verifying,
        Extracting,
        Installing,
        Configuring,
        Cancelling,
        Completed,
        Failed,
        Cancelled
    };

    struct HubTaskProgress final
    {
        std::uint64_t BytesTransferred = 0;
        std::uint64_t TotalBytes = 0;
        std::uint64_t BytesPerSecond = 0;
        std::uint32_t Attempt = 0;
        std::string CurrentPackage;
        std::uint32_t RemainingComponents = 0;
        std::string Phase;
    };

    struct HubTask final
    {
        std::string Id;
        HubTaskKind Kind = HubTaskKind::Download;
        std::string DisplayName;
        std::vector<std::string> PackageIds;
        std::optional<std::string> TargetInstallationId;
        HubTaskState State = HubTaskState::Queued;
        HubTaskProgress Progress;
        std::uint64_t CreatedUnixSeconds = 0;
        std::uint64_t UpdatedUnixSeconds = 0;
        std::optional<std::uint64_t> WorkerProcessId;
        std::optional<HubError> Failure;
    };

    [[nodiscard]] bool IsTerminal(HubTaskState state) noexcept;
    [[nodiscard]] bool IsValidTaskTransition(HubTaskState from, HubTaskState to) noexcept;

    class HubTaskStore final
    {
      public:
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        explicit HubTaskStore(std::filesystem::path storePath);

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] HubStatus Add(HubTask task);
        [[nodiscard]] HubStatus Claim(const std::string& taskId, HubTaskState state, std::uint64_t workerProcessId,
                                      std::uint64_t updatedUnixSeconds);
        [[nodiscard]] HubStatus Transition(const std::string& taskId, HubTaskState state,
                                           std::uint64_t updatedUnixSeconds, std::optional<HubError> failure = {});
        [[nodiscard]] HubStatus UpdateProgress(const std::string& taskId, HubTaskProgress progress,
                                               std::uint64_t updatedUnixSeconds);
        [[nodiscard]] HubStatus SetWorkerProcess(const std::string& taskId, std::optional<std::uint64_t> processId,
                                                 std::uint64_t updatedUnixSeconds);
        [[nodiscard]] HubStatus RemoveTerminal(const std::string& taskId);

        [[nodiscard]] std::shared_ptr<const std::vector<HubTask>> Snapshot() const noexcept;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

      private:
        [[nodiscard]] HubStatus Commit(std::vector<HubTask> tasks);

        std::filesystem::path m_Path;
        std::shared_ptr<const std::vector<HubTask>> m_Snapshot;
    };
} // namespace KeireHub
