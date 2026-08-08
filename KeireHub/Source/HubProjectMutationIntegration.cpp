#include "KeireHub/HubProjectMutationIntegration.h"

#include "KeireHub/HubProjectWorkflow.h"

#include <utility>

namespace KeireHub
{
    HubProjectMutationServices CreateHubProjectMutationServices(HubProjectWorkflow& workflow)
    {
        return {.PrepareDuplicate = [&workflow](const std::string& projectId, const std::filesystem::path& destination,
                                                std::string displayName)
                { return workflow.PrepareDuplicate(projectId, destination, std::move(displayName)); },
                .StageDuplicate = [&workflow](ProjectDuplicatePlan plan, const ProjectDuplicateCallbacks& callbacks)
                { return workflow.StageDuplicate(std::move(plan), callbacks); },
                .CommitDuplicate = [&workflow](ProjectDuplicateStagedResult staged)
                { return workflow.CommitDuplicate(std::move(staged)); },
                .DiscardDuplicate = [&workflow](const ProjectDuplicateStagedResult& staged)
                { return workflow.DiscardDuplicate(staged); }};
    }
} // namespace KeireHub
