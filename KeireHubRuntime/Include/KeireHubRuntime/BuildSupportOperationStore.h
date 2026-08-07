#pragma once

#include "KeireHubRuntime/BuildSupportPlanning.h"
#include "KeireHubRuntime/HubError.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    enum class BuildSupportOperationState
    {
        Launching,
        Running,
        Cancelling,
        Completed,
        Failed,
        Cancelled
    };

    struct BuildSupportOperationRecord final
    {
        std::string Id;
        BuildSupportOperationKind Kind = BuildSupportOperationKind::Import;
        BuildSupportOperationState State = BuildSupportOperationState::Launching;
        std::string TargetInstallationId;
        std::string EngineVersion;
        std::filesystem::path EditorRoot;
        std::filesystem::path AssetToolEntrypoint;
        std::filesystem::path OperationRoot;
        std::filesystem::path StatusPath;
        std::filesystem::path CancelPath;
        std::string ComponentId;
        std::string CurrentPackage;
        std::optional<std::uint64_t> ChildProcessId;
        std::string Phase;
        std::string Message;
        float Progress = 0.0F;
        std::uint64_t CreatedUnixSeconds = 0;
        std::uint64_t UpdatedUnixSeconds = 0;

        [[nodiscard]] bool operator==(const BuildSupportOperationRecord&) const = default;
    };

    [[nodiscard]] bool IsActive(BuildSupportOperationState state) noexcept;
    [[nodiscard]] bool IsTerminal(BuildSupportOperationState state) noexcept;
    [[nodiscard]] std::string BuildSupportTaskId(std::string_view operationId);
    [[nodiscard]] HubResult<bool> HasPendingBuildSupportRemovalJournal(const std::filesystem::path& storageRoot,
                                                                       std::string_view engineVersion,
                                                                       std::string_view componentId);

    struct BuildSupportRemovalInventoryGate final
    {
        std::uint64_t BaselineRevision = 0;
        bool RefreshAfterCurrentLoad = false;
    };

    enum class BuildSupportRemovalInventoryAction
    {
        Wait,
        StartFreshRefresh,
        Reconcile
    };

    [[nodiscard]] BuildSupportRemovalInventoryAction
    EvaluateBuildSupportRemovalInventory(const BuildSupportRemovalInventoryGate& gate, std::uint64_t revision,
                                         bool loading) noexcept;

    class BuildSupportOperationStore final
    {
      public:
        static constexpr std::uint32_t CurrentSchemaVersion = 1;
        static constexpr std::size_t MaximumRecords = 128;
        static constexpr std::size_t MaximumTerminalHistory = 64;

        explicit BuildSupportOperationStore(std::filesystem::path operationRoot);

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] HubStatus Add(BuildSupportOperationRecord record);
        [[nodiscard]] HubStatus AttachProcess(const std::string& operationId, std::uint64_t processId,
                                              std::uint64_t updatedUnixSeconds);
        [[nodiscard]] HubStatus Update(const std::string& operationId, BuildSupportOperationState state,
                                       std::string phase, std::string message, float progress,
                                       std::uint64_t updatedUnixSeconds);
        [[nodiscard]] HubStatus Finish(const std::string& operationId, BuildSupportOperationState state,
                                       std::string phase, std::string message, float progress,
                                       std::uint64_t updatedUnixSeconds);

        [[nodiscard]] std::shared_ptr<const std::vector<BuildSupportOperationRecord>> Snapshot() const noexcept;
        [[nodiscard]] const std::filesystem::path& Root() const noexcept;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

      private:
        [[nodiscard]] HubStatus Commit(std::vector<BuildSupportOperationRecord> records);

        std::filesystem::path m_Root;
        std::filesystem::path m_Path;
        std::shared_ptr<const std::vector<BuildSupportOperationRecord>> m_Snapshot;
    };
} // namespace KeireHub
