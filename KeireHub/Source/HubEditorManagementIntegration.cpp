#include "KeireHub/HubEditorManagementIntegration.h"

#include "KeireHub/HubEditorInstallWorkflow.h"
#include "KeireHub/HubManagedEditorRemovalIntegration.h"
#include "KeireHub/HubManagedEditorRepairIntegration.h"
#include "KeireHub/HubPackageTaskWorkflow.h"

#include "Keire/Log.h"

#include <utility>

namespace KeireHub
{
    HubStatus BeginHubEditorManagementCommand(HubEditorManagementWorkflow& workflow, HubEditorInstallWorkflow* installs,
                                              HubPackageTaskWorkflow* tasks, const HubUiCommand& command,
                                              std::string& notice, bool& noticeError)
    {
        if ((command.Type == HubUiCommandType::RepairManagedEditor && (!installs || !tasks)) ||
            (command.Type == HubUiCommandType::RemoveManagedEditor && !tasks))
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "Managed editor package tasks are unavailable.",
                                       .AffectedItem = command.ItemId});
        }
        if (const auto status = workflow.Execute(command); !status)
            return status;
        switch (command.Type)
        {
        case HubUiCommandType::VerifyEditor:
            notice = "Editor verification started. Progress is available in the task center.";
            break;
        case HubUiCommandType::RepairManagedEditor:
            notice = "Checking the managed editor before repair.";
            break;
        case HubUiCommandType::RemoveManagedEditor:
            notice = "Verifying managed editor ownership and contents before uninstall.";
            break;
        case HubUiCommandType::RemoveExternalEditor:
            notice = "External editor removed from the Hub. Its files were not deleted.";
            break;
        case HubUiCommandType::RemoveMissingManagedEditor:
            notice = "Missing editor registration removed. No files were deleted; this version can now be installed.";
            break;
        default:
            notice = "Editor installation check started.";
            break;
        }
        noticeError = false;
        return HubStatus::Success();
    }

    void PollHubEditorManagement(HubEditorManagementWorkflow& workflow, HubEditorInstallWorkflow* installs,
                                 HubPackageTaskWorkflow* tasks, std::string& notice, bool& noticeError)
    {
        const auto polled = workflow.Poll();
        if (!polled)
        {
            notice = polled.Error().Message;
            noticeError = true;
            return;
        }
        auto completion = workflow.TakeCompletion();
        if (!completion)
            return;
        if (completion->Failure)
        {
            if (!completion->Failure->TechnicalDetails.empty())
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Editor management failed [{}]: {}",
                                   ToString(completion->Failure->Code), completion->Failure->TechnicalDetails);
            }
            notice = completion->Failure->Message;
            noticeError = true;
            return;
        }

        if (completion->Operation == HubEditorManagementOperation::Refresh)
        {
            notice = "Editor installation refresh completed.";
            noticeError = false;
            return;
        }
        if (completion->Operation == HubEditorManagementOperation::Verify)
        {
            switch (completion->VerifiedHealth.value_or(InstallationHealth::Unknown))
            {
            case InstallationHealth::Healthy:
                notice = "Editor installation verified and ready.";
                noticeError = false;
                break;
            case InstallationHealth::Missing:
                notice = "The editor folder is missing. Remove its stale Hub registration to reinstall this version.";
                noticeError = true;
                break;
            case InstallationHealth::Damaged:
                notice = "Verification found damaged or incomplete editor files. Repair the managed installation.";
                noticeError = true;
                break;
            case InstallationHealth::VerificationRequired:
                notice = "The editor could not be fully verified on this host. Review its compatibility details.";
                noticeError = true;
                break;
            case InstallationHealth::Unknown:
                notice = "Editor verification completed without a conclusive health result.";
                noticeError = true;
                break;
            }
            return;
        }
        if (!completion->Authorization || !tasks)
        {
            notice = "The package task center became unavailable before editor preparation completed.";
            noticeError = true;
            return;
        }

        HubResult<std::string> queued = HubResult<std::string>::Failure(
            {.Code = HubErrorCode::InvalidTransition, .Message = "The editor operation could not be queued."});
        if (completion->Operation == HubEditorManagementOperation::AuthorizeRepair)
        {
            if (!installs)
            {
                notice = "The verified editor repair catalog became unavailable.";
                noticeError = true;
                return;
            }
            queued = ExecuteHubManagedEditorRepairPlan(*completion->Authorization, *installs, *tasks);
        }
        else if (completion->Operation == HubEditorManagementOperation::AuthorizeRemoval)
        {
            queued = ExecuteHubManagedEditorRemovalPlan(*completion->Authorization, *tasks);
        }
        if (!queued)
        {
            notice = queued.Error().Message;
            noticeError = true;
            return;
        }
        notice = std::move(queued).Value();
        noticeError = false;
    }
} // namespace KeireHub
