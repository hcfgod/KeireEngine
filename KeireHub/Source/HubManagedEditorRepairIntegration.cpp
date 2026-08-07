#include "KeireHub/HubManagedEditorRepairIntegration.h"

#include "KeireHub/HubEditorInstallWorkflow.h"
#include "KeireHub/HubPackageTaskWorkflow.h"

namespace KeireHub
{
    HubResult<std::string> ExecuteHubManagedEditorRepairPlan(const EditorManagedOperationPlan& authorization,
                                                             HubEditorInstallWorkflow& installs,
                                                             HubPackageTaskWorkflow& tasks)
    {
        auto plan = installs.PreviewRepair(authorization);
        if (!plan)
            return HubResult<std::string>::Failure(plan.Error());
        if (const auto status = tasks.QueueEditorRepair(plan.Value(), installs.EndpointContext()); !status)
            return HubResult<std::string>::Failure(status.Error());
        return HubResult<std::string>::Success(
            "Managed editor repair queued. The task center will report its progress.");
    }
} // namespace KeireHub
