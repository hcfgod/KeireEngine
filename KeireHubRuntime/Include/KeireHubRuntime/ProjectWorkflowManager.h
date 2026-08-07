#pragma once

#include "KeireHubRuntime/HubProjectCatalog.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace KeireHub
{
    struct ProjectWorkflowServices final
    {
        std::function<HubResult<std::string>()> GenerateProjectId;
        std::function<HubResult<std::string>()> CurrentUtcTimestamp;
        std::function<std::uint64_t()> CurrentUnixSeconds;
        std::function<HubResult<bool>(const std::filesystem::path&)> IsProjectLocked;
    };

    struct ProjectDuplicateRequest final
    {
        std::string SourceProjectId;
        std::filesystem::path Destination;
        std::string DisplayName;
        std::uint64_t MaximumCopiedBytes = 256ULL * 1024ULL * 1024ULL * 1024ULL;
        std::size_t MaximumCopiedEntries = 100'000;
        std::function<HubStatus(const std::filesystem::path&)> ValidateStagedProject;
    };

    struct ProjectDuplicateResult final
    {
        std::string ProjectId;
        std::filesystem::path Root;
        std::string DisplayName;
        std::uint64_t CopiedBytes = 0;
        std::size_t CopiedEntries = 0;
    };

    struct ProjectDuplicateCallbacks final
    {
        // Callbacks are borrowed only for the synchronous duration of StageDuplicate and may run on its caller thread.
        std::function<bool()> IsCancelled;
        std::function<void(std::uint64_t, std::size_t)> ReportProgress;
    };

    struct ProjectDuplicatePlan final
    {
        ProjectDuplicateRequest Request;
        std::filesystem::path SourceRoot;
        std::filesystem::path Destination;
        std::filesystem::path Staging;
        std::string NewProjectId;
        std::string CreatedAt;
        std::uint64_t AddedUnixSeconds = 0;
        std::optional<std::string> PreferredEditorInstallationId;
    };

    enum class ProjectDuplicateStageState
    {
        ReadyToCommit,
        Cancelled
    };

    struct ProjectDuplicateStagedResult final
    {
        ProjectDuplicatePlan Plan;
        ProjectDuplicateStageState State = ProjectDuplicateStageState::ReadyToCommit;
        std::uint64_t CopiedBytes = 0;
        std::size_t CopiedEntries = 0;
    };

    // The caller retains the catalog and invokes this manager on the catalog owner thread.
    class ProjectWorkflowManager final
    {
      public:
        static constexpr std::uint32_t SupportedProjectSchema = 3;

        explicit ProjectWorkflowManager(HubProjectCatalog& catalog, ProjectWorkflowServices services = {});

        [[nodiscard]] HubResult<ProjectDuplicateResult> Duplicate(const ProjectDuplicateRequest& request);
        [[nodiscard]] HubResult<ProjectDuplicatePlan> PrepareDuplicate(const ProjectDuplicateRequest& request);
        // Failure and cancellation remove any staging created by this call. A ready result remains unpublished until
        // CommitDuplicate and can be removed idempotently with DiscardDuplicate.
        [[nodiscard]] HubResult<ProjectDuplicateStagedResult>
        StageDuplicate(ProjectDuplicatePlan plan, const ProjectDuplicateCallbacks& callbacks = {}) const;
        [[nodiscard]] HubResult<ProjectDuplicateResult> CommitDuplicate(ProjectDuplicateStagedResult staged);
        [[nodiscard]] HubStatus DiscardDuplicate(const ProjectDuplicateStagedResult& staged) const;
        [[nodiscard]] HubStatus LocateMovedProject(const std::string& projectId,
                                                   const std::filesystem::path& candidateRoot);
        [[nodiscard]] HubStatus RenameDisplayName(const std::string& projectId, const std::string& displayName);
        [[nodiscard]] HubStatus RemoveFromHub(const std::string& projectId);

      private:
        HubProjectCatalog& m_Catalog;
        ProjectWorkflowServices m_Services;
    };
} // namespace KeireHub
