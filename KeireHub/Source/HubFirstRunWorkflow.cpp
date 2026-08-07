#include "KeireHub/HubFirstRunWorkflow.h"

#include "Keire/Log.h"

#include <exception>
#include <utility>

namespace KeireHub
{
    HubFirstRunWorkflow::HubFirstRunWorkflow(HubFirstRunImportPreparationHooks preparationHooks)
        : m_PreparationHooks(std::move(preparationHooks)),
          m_Snapshot(std::make_shared<const HubFirstRunWorkflowSnapshot>())
    {
    }

    HubFirstRunWorkflow::~HubFirstRunWorkflow() { Cancel(); }

    HubStatus HubFirstRunWorkflow::Start(HubFirstRunDiscoveryRequest request, const std::uint64_t nowUnixSeconds)
    {
        if (nowUnixSeconds == 0)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                       .Message = "The first-run import timestamp is invalid.",
                                       .AffectedItem = "first-run-discovery"});
        }
        if (Snapshot()->State == HubFirstRunWorkflowState::Running)
        {
            return HubStatus::Failure({.Code = HubErrorCode::InstallationBusy,
                                       .Message = "First-run discovery is already in progress.",
                                       .AffectedItem = "first-run-discovery"});
        }
        if (m_Worker.joinable())
            m_Worker.join();
        Publish({.State = HubFirstRunWorkflowState::Running, .Message = "Checking only the selected locations."});
        m_Worker = std::jthread(
            [this, request = std::move(request), nowUnixSeconds,
             preparationHooks = m_PreparationHooks](const std::stop_token stop) mutable
            {
                try
                {
                    HubFirstRunDiscovery discovery;
                    const auto status =
                        discovery.Discover(request, {.IsCancelled = [stop] { return stop.stop_requested(); },
                                                     .ReportProgress =
                                                         [this](const HubFirstRunDiscoveryProgress& progress)
                                                     {
                                                         Publish({.State = HubFirstRunWorkflowState::Running,
                                                                  .EntriesVisited = progress.EntriesVisited,
                                                                  .ProjectsFound = progress.ProjectsFound,
                                                                  .EditorsFound = progress.EditorsFound,
                                                                  .Message = "Checking only the selected locations."});
                                                     }});
                    const auto result = discovery.Snapshot();
                    if (!status)
                    {
                        Publish({.State = HubFirstRunWorkflowState::Failed,
                                 .Message = status.Error().Message,
                                 .Discovery = result});
                        return;
                    }
                    const auto cancelled = result->State == HubFirstRunDiscoveryState::Cancelled;
                    if (cancelled)
                    {
                        Publish({.State = HubFirstRunWorkflowState::Cancelled,
                                 .EntriesVisited = result->EntriesVisited,
                                 .ProjectsFound = result->Projects.size(),
                                 .EditorsFound = result->Editors.size(),
                                 .Message = "Optional discovery was cancelled.",
                                 .Discovery = result});
                        return;
                    }
                    Publish({.State = HubFirstRunWorkflowState::Running,
                             .EntriesVisited = result->EntriesVisited,
                             .ProjectsFound = result->Projects.size(),
                             .EditorsFound = result->Editors.size(),
                             .Message = "Validating discovered projects and editors.",
                             .Discovery = result});
                    const auto injectedCancellation = std::move(preparationHooks.IsCancelled);
                    preparationHooks.IsCancelled = [stop, injectedCancellation]
                    { return stop.stop_requested() || (injectedCancellation && injectedCancellation()); };
                    auto prepared = PrepareHubFirstRunImport(*result, nowUnixSeconds, preparationHooks);
                    if (!prepared)
                    {
                        const bool preparationCancelled =
                            stop.stop_requested() || (preparationHooks.IsCancelled && preparationHooks.IsCancelled());
                        Publish({.State = preparationCancelled ? HubFirstRunWorkflowState::Cancelled
                                                               : HubFirstRunWorkflowState::Failed,
                                 .EntriesVisited = result->EntriesVisited,
                                 .ProjectsFound = result->Projects.size(),
                                 .EditorsFound = result->Editors.size(),
                                 .Message = preparationCancelled ? "Optional discovery was cancelled."
                                                                 : prepared.Error().Message,
                                 .Discovery = result});
                        return;
                    }
                    if (stop.stop_requested())
                    {
                        Publish({.State = HubFirstRunWorkflowState::Cancelled,
                                 .EntriesVisited = result->EntriesVisited,
                                 .ProjectsFound = result->Projects.size(),
                                 .EditorsFound = result->Editors.size(),
                                 .Message = "Optional discovery was cancelled.",
                                 .Discovery = result});
                        return;
                    }
                    Publish({.State = HubFirstRunWorkflowState::Completed,
                             .EntriesVisited = result->EntriesVisited,
                             .ProjectsFound = result->Projects.size(),
                             .EditorsFound = result->Editors.size(),
                             .Message = "Selected locations checked and ready to import.",
                             .Discovery = result,
                             .PreparedImport =
                                 std::make_shared<const HubFirstRunPreparedImport>(std::move(prepared).Value())});
                }
                catch (const std::exception& error)
                {
                    KEIRE_CLIENT_ERROR("[Project Hub] First-run discovery failed: {}", error.what());
                    Publish({.State = HubFirstRunWorkflowState::Failed,
                             .Message = "Discovery failed unexpectedly. See the Hub log for details."});
                }
                catch (...)
                {
                    Publish({.State = HubFirstRunWorkflowState::Failed,
                             .Message = "Discovery failed unexpectedly. See the Hub log for details."});
                }
            });
        return HubStatus::Success();
    }

    void HubFirstRunWorkflow::Cancel() noexcept
    {
        if (!m_Worker.joinable())
            return;
        m_Worker.request_stop();
        m_Worker.join();
    }

    std::shared_ptr<const HubFirstRunWorkflowSnapshot> HubFirstRunWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    void HubFirstRunWorkflow::Publish(HubFirstRunWorkflowSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::make_shared<const HubFirstRunWorkflowSnapshot>(std::move(snapshot));
    }
} // namespace KeireHub
