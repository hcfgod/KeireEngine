#include "KeireHub/HubEditorInstallWorkflow.h"

#include "KeireHub/HubPackageTaskWorkflow.h"

#include <utility>

namespace KeireHub
{
    HubResult<std::string> ExecuteHubEditorInstallCommand(const HubUiCommand& command,
                                                          HubEditorInstallWorkflow& installs,
                                                          HubPackageTaskWorkflow* packageTasks)
    {
        if ((command.Type != HubUiCommandType::PreviewEditorInstall &&
             command.Type != HubUiCommandType::InstallEditor) ||
            !command.EditorInstall)
        {
            return HubResult<std::string>::Failure({.Code = HubErrorCode::InvalidArgument,
                                                    .Message = "The editor installation request is invalid.",
                                                    .AffectedItem = command.ItemId});
        }

        auto plan = installs.PreviewInstall(*command.EditorInstall);
        if (!plan)
            return HubResult<std::string>::Failure(plan.Error());
        if (command.Type == HubUiCommandType::PreviewEditorInstall)
            return HubResult<std::string>::Success("Editor install plan is ready for review.");
        if (!packageTasks)
        {
            return HubResult<std::string>::Failure({.Code = HubErrorCode::WorkerInterrupted,
                                                    .Message = "The package task center is unavailable.",
                                                    .Retryable = true,
                                                    .AffectedItem = plan.Value().EditorPackageId});
        }
        if (const auto queued = packageTasks->QueueEditorInstall(plan.Value(), installs.EndpointContext()); !queued)
            return HubResult<std::string>::Failure(queued.Error());
        installs.ClearPreview();
        return HubResult<std::string>::Success("Editor installation queued.");
    }
} // namespace KeireHub
