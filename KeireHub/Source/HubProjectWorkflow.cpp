#include "KeireHub/HubProjectWorkflow.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubStatus ValidateStagedProject(const std::filesystem::path& root)
        {
            const auto inspection = Keire::Project::InspectMetadata(root);
            if (inspection.Status == Keire::ProjectStatus::Ready)
                return HubStatus::Success();
            return HubStatus::Failure({.Code = HubErrorCode::ProjectValidationFailed,
                                       .Message = "The staged duplicate did not pass project validation.",
                                       .AffectedItem = inspection.Name,
                                       .TechnicalDetails = inspection.Diagnostic});
        }

        void ApplyDescriptorMetadata(HubRecentProject& recent, const Keire::ProjectDescriptor& descriptor)
        {
            recent.CachedMetadata.CreatedWithEngineVersion = descriptor.CreatedWithEngineVersion;
            recent.CachedMetadata.LastSavedWithEngineVersion = descriptor.LastSavedWithEngineVersion;
            recent.CachedMetadata.MinimumEngineVersion = descriptor.MinimumEngineVersion;
            recent.CachedMetadata.ProjectSchemaVersion = descriptor.SchemaVersion;
            recent.CachedMetadata.Status = HubProjectStatus::Ready;
        }

    } // namespace

    HubResult<ProjectDuplicateResult> HubProjectWorkflow::Duplicate(const std::string& projectId,
                                                                    const std::filesystem::path& destination,
                                                                    std::string displayName)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return HubResult<ProjectDuplicateResult>::Failure(status.Error());
        ProjectDuplicateRequest request{.SourceProjectId = projectId,
                                        .Destination = destination,
                                        .DisplayName = std::move(displayName),
                                        .ValidateStagedProject = &ValidateStagedProject};
        return m_Workflows.Duplicate(request);
    }

    HubResult<ProjectDuplicatePlan> HubProjectWorkflow::PrepareDuplicate(const std::string& projectId,
                                                                         const std::filesystem::path& destination,
                                                                         std::string displayName)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return HubResult<ProjectDuplicatePlan>::Failure(status.Error());
        ProjectDuplicateRequest request{.SourceProjectId = projectId,
                                        .Destination = destination,
                                        .DisplayName = std::move(displayName),
                                        .ValidateStagedProject = &ValidateStagedProject};
        return m_Workflows.PrepareDuplicate(request);
    }

    HubResult<ProjectDuplicateStagedResult>
    HubProjectWorkflow::StageDuplicate(ProjectDuplicatePlan plan, const ProjectDuplicateCallbacks& callbacks) const
    {
        return m_Workflows.StageDuplicate(std::move(plan), callbacks);
    }

    HubResult<ProjectDuplicateResult> HubProjectWorkflow::CommitDuplicate(ProjectDuplicateStagedResult staged)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return HubResult<ProjectDuplicateResult>::Failure(status.Error());
        return m_Workflows.CommitDuplicate(std::move(staged));
    }

    HubStatus HubProjectWorkflow::DiscardDuplicate(const ProjectDuplicateStagedResult& staged) const
    {
        return m_Workflows.DiscardDuplicate(staged);
    }

    HubStatus HubProjectWorkflow::LocateMovedProject(const std::string& projectId,
                                                     const std::filesystem::path& candidateRoot)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return status;
        return m_Workflows.LocateMovedProject(projectId, candidateRoot);
    }

    HubStatus HubProjectWorkflow::RenameDisplayName(const std::string& projectId, const std::string& displayName)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return status;
        return m_Workflows.RenameDisplayName(projectId, displayName);
    }

    HubStatus HubProjectWorkflow::RemoveFromHub(const std::string& projectId)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return status;
        return m_Workflows.RemoveFromHub(projectId);
    }

    HubStatus HubProjectWorkflow::Add(const std::filesystem::path& root, const std::uint64_t nowUnixSeconds)
    {
        return Add(Keire::Project::InspectMetadata(root), nowUnixSeconds);
    }

    HubStatus HubProjectWorkflow::RecordOpened(const Keire::Project& project, std::string preferredEditorInstallationId,
                                               const std::uint64_t nowUnixSeconds)
    {
        return RecordOpened(project.Root(), project.Descriptor(), std::move(preferredEditorInstallationId),
                            nowUnixSeconds);
    }

    HubStatus HubProjectWorkflow::RecordOpened(const std::filesystem::path& root,
                                               const Keire::ProjectDescriptor& descriptor,
                                               std::string preferredEditorInstallationId,
                                               const std::uint64_t nowUnixSeconds)
    {
        if (const auto status = ReloadAuthoritativeCatalog(); !status)
            return status;
        const auto id = descriptor.Id.ToString();
        HubRecentProject recent;
        const auto projects = m_Controller.Projects().Snapshot();
        if (const auto found = std::ranges::find(*projects, id, &HubRecentProject::Id); found != projects->end())
            recent = *found;
        else
        {
            recent.Id = id;
            recent.AddedUnixSeconds = nowUnixSeconds;
            recent.CachedMetadata.CreatedUnixSeconds = nowUnixSeconds;
        }
        recent.Root = root;
        recent.Name = descriptor.Name;
        recent.LastOpenedUnixSeconds = nowUnixSeconds;
        if (!preferredEditorInstallationId.empty())
            recent.PreferredEditorInstallationId = std::move(preferredEditorInstallationId);
        ApplyDescriptorMetadata(recent, descriptor);
        return m_Controller.Projects().Upsert(std::move(recent));
    }
} // namespace KeireHub
