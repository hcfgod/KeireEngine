#pragma once

#include "KeireHubRuntime/HubController.h"
#include "KeireHubRuntime/ProjectWorkflowManager.h"

#include "Keire/Project/Project.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace KeireHub
{
    // Project mutations use HubProjectCatalog as the only writer. ProjectRegistry remains a presentation reader.
    class HubProjectWorkflow final
    {
      public:
        explicit HubProjectWorkflow(HubController& controller);

        [[nodiscard]] HubResult<ProjectDuplicateResult>
        Duplicate(const std::string& projectId, const std::filesystem::path& destination, std::string displayName);
        [[nodiscard]] HubResult<ProjectDuplicatePlan> PrepareDuplicate(const std::string& projectId,
                                                                       const std::filesystem::path& destination,
                                                                       std::string displayName);
        [[nodiscard]] HubResult<ProjectDuplicateStagedResult>
        StageDuplicate(ProjectDuplicatePlan plan, const ProjectDuplicateCallbacks& callbacks = {}) const;
        [[nodiscard]] HubResult<ProjectDuplicateResult> CommitDuplicate(ProjectDuplicateStagedResult staged);
        [[nodiscard]] HubStatus DiscardDuplicate(const ProjectDuplicateStagedResult& staged) const;
        [[nodiscard]] HubStatus LocateMovedProject(const std::string& projectId,
                                                   const std::filesystem::path& candidateRoot);
        [[nodiscard]] HubStatus RenameDisplayName(const std::string& projectId, const std::string& displayName);
        [[nodiscard]] HubStatus RemoveFromHub(const std::string& projectId);
        [[nodiscard]] HubStatus SetPinned(const std::string& projectId, bool pinned);
        [[nodiscard]] HubStatus Add(const std::filesystem::path& root, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus Add(const Keire::ProjectInspectionResult& inspection, std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus RecordOpened(const Keire::Project& project, std::string preferredEditorInstallationId,
                                             std::uint64_t nowUnixSeconds);
        [[nodiscard]] HubStatus RecordOpened(const std::filesystem::path& root,
                                             const Keire::ProjectDescriptor& descriptor,
                                             std::string preferredEditorInstallationId, std::uint64_t nowUnixSeconds);

      private:
        [[nodiscard]] HubStatus ReloadAuthoritativeCatalog();

        HubController& m_Controller;
        ProjectWorkflowManager m_Workflows;
    };
} // namespace KeireHub
