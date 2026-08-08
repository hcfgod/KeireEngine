#include "KeireHub/HubManagedEditorRemovalIntegration.h"

#include "KeireHub/HubPackageTaskWorkflow.h"

namespace KeireHub
{
    HubResult<std::string> ExecuteHubManagedEditorRemovalPlan(const EditorManagedOperationPlan& plan,
                                                              HubPackageTaskWorkflow& tasks)
    {
        if (const auto status = tasks.QueueEditorRemoval(plan); !status)
            return HubResult<std::string>::Failure(status.Error());
        return HubResult<std::string>::Success(
            "Managed editor uninstall queued. The task center will report its progress.");
    }
} // namespace KeireHub
