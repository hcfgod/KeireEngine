#include "KeireHub/HubMaintenanceIntegration.h"

#include "KeireHub/HubPackageTaskWorkflow.h"

#include "Keire/Log.h"

namespace KeireHub
{
    HubStatus BeginHubVerifiedCacheClear(HubMaintenanceWorkflow& maintenance, HubController& controller,
                                         std::unique_ptr<HubPackageTaskWorkflow>& packageTasks,
                                         bool& packageTaskRefreshPending)
    {
        if (maintenance.Snapshot()->IsRunning())
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                       .Message = "The verified package cache is already being cleared.",
                                       .Retryable = true,
                                       .AffectedItem = "cache"});
        }
        auto tasks = controller.Tasks().Snapshot();
        if (packageTasks)
        {
            const auto worker = packageTasks->Snapshot();
            if (worker->State != HubWorkerCoordinatorState::Ready || !worker->Tasks)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                           .Message = "Wait for the package task center to become idle before "
                                                      "clearing the cache.",
                                           .AffectedItem = "cache"});
            }
            tasks = worker->Tasks;
        }
        if (const auto validation = ValidateVerifiedPackageCacheClear(*tasks); !validation)
            return validation;

        if (packageTasks)
        {
            packageTasks->Stop();
            packageTasks.reset();
        }
        packageTaskRefreshPending = false;
        auto started = maintenance.StartClearVerifiedCache(*tasks, controller.Settings().Snapshot()->CacheRoot);
        if (!started)
        {
            packageTaskRefreshPending = true;
            return HubStatus::Failure(started.Error());
        }
        return HubStatus::Success();
    }

    void PollHubMaintenance(HubMaintenanceWorkflow& maintenance, bool& packageTaskRefreshPending, std::string& notice,
                            bool& noticeError)
    {
        const auto polled = maintenance.Poll();
        if (!polled)
        {
            notice = polled.Error().Message;
            noticeError = true;
            return;
        }
        const auto completed = maintenance.TakeCompletion();
        if (!completed)
            return;

        packageTaskRefreshPending = true;
        if (completed->Failure)
        {
            if (!completed->Failure->TechnicalDetails.empty())
            {
                KEIRE_CLIENT_ERROR("[Project Hub] Cache maintenance failed [{}]: {}",
                                   ToString(completed->Failure->Code), completed->Failure->TechnicalDetails);
            }
            notice = completed->Failure->Message;
            noticeError = true;
            return;
        }
        notice = "Verified package cache cleared.";
        noticeError = false;
    }
} // namespace KeireHub
