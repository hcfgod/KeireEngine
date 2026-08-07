#include "KeireHub/HubProjectUpgradeWorkflow.h"

#include <exception>
#include <string>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError UpgradeError(const HubErrorCode code, std::string message,
                                            const std::filesystem::path& root, std::string details = {},
                                            const bool retryable = true)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = root.generic_string(),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] HubStatus UnexpectedFailure(std::string message, const std::filesystem::path& root,
                                                  const std::exception& error)
        {
            return HubStatus::Failure(
                UpgradeError(HubErrorCode::MigrationFailed, std::move(message), root, error.what()));
        }

        [[nodiscard]] HubStatus UnexpectedFailure(std::string message, const std::filesystem::path& root)
        {
            return HubStatus::Failure(UpgradeError(HubErrorCode::MigrationFailed, std::move(message), root,
                                                   "The operation failed with a non-standard exception."));
        }
    } // namespace

    bool HubProjectUpgradeWorkflowSnapshot::IsActive() const noexcept
    {
        return State == HubProjectUpgradeWorkflowState::Inspecting ||
               State == HubProjectUpgradeWorkflowState::Applying ||
               State == HubProjectUpgradeWorkflowState::Recovering ||
               State == HubProjectUpgradeWorkflowState::RollingBack;
    }

    HubProjectUpgradeWorkflowServices CreateHubProjectUpgradeWorkflowServices()
    {
        return {.Inspect =
                    [](const std::filesystem::path& root, const std::span<const Keire::ProjectUpgradeStep> upgrades)
                {
                    try
                    {
                        Keire::ProjectUpgradeService service(root, std::vector(upgrades.begin(), upgrades.end()));
                        const bool interrupted = service.State() == Keire::ProjectUpgradeTransactionState::Interrupted;
                        HubProjectUpgradePreparation preparation{.Interrupted = interrupted};
                        if (!interrupted)
                            preparation.Plan = service.Plan();
                        return HubResult<HubProjectUpgradePreparation>::Success(std::move(preparation));
                    }
                    catch (const std::exception& error)
                    {
                        return HubResult<HubProjectUpgradePreparation>::Failure(
                            UnexpectedFailure("The project upgrade could not be inspected.", root, error).Error());
                    }
                    catch (...)
                    {
                        return HubResult<HubProjectUpgradePreparation>::Failure(
                            UnexpectedFailure("The project upgrade could not be inspected.", root).Error());
                    }
                },
                .Apply =
                    [](const std::filesystem::path& root, const std::span<const Keire::ProjectUpgradeStep> upgrades,
                       const Keire::ProjectUpgradePlan& plan)
                {
                    try
                    {
                        Keire::ProjectUpgradeService service(root, std::vector(upgrades.begin(), upgrades.end()));
                        service.Apply(plan);
                        return HubStatus::Success();
                    }
                    catch (const std::exception& error)
                    {
                        return UnexpectedFailure("The project upgrade could not be applied.", root, error);
                    }
                    catch (...)
                    {
                        return UnexpectedFailure("The project upgrade could not be applied.", root);
                    }
                },
                .Recover =
                    [](const std::filesystem::path& root, const std::span<const Keire::ProjectUpgradeStep> upgrades)
                {
                    try
                    {
                        Keire::ProjectUpgradeService service(root, std::vector(upgrades.begin(), upgrades.end()));
                        service.Recover();
                        return HubStatus::Success();
                    }
                    catch (const std::exception& error)
                    {
                        return UnexpectedFailure("The project upgrade could not be recovered.", root, error);
                    }
                    catch (...)
                    {
                        return UnexpectedFailure("The project upgrade could not be recovered.", root);
                    }
                },
                .Rollback =
                    [](const std::filesystem::path& root, const std::span<const Keire::ProjectUpgradeStep> upgrades)
                {
                    try
                    {
                        Keire::ProjectUpgradeService service(root, std::vector(upgrades.begin(), upgrades.end()));
                        service.Rollback();
                        return HubStatus::Success();
                    }
                    catch (const std::exception& error)
                    {
                        return UnexpectedFailure("The project upgrade could not be rolled back.", root, error);
                    }
                    catch (...)
                    {
                        return UnexpectedFailure("The project upgrade could not be rolled back.", root);
                    }
                }};
    }

    HubProjectUpgradeWorkflow::HubProjectUpgradeWorkflow(HubProjectUpgradeWorkflowServices services)
        : m_Services(std::move(services)), m_OwnerThread(std::this_thread::get_id()),
          m_Snapshot(std::make_shared<const HubProjectUpgradeWorkflowSnapshot>())
    {
    }

    HubProjectUpgradeWorkflow::~HubProjectUpgradeWorkflow() { Stop(); }

    HubStatus HubProjectUpgradeWorkflow::Start(std::filesystem::path root,
                                               const std::span<const Keire::ProjectUpgradeStep> upgrades)
    {
        if (const auto owner = RequireOwnerThread("inspect"); !owner)
            return owner;
        if (root.empty())
        {
            return HubStatus::Failure(UpgradeError(HubErrorCode::InvalidArgument,
                                                   "The project upgrade requires a project folder.", root, {}, false));
        }
        if (Snapshot()->IsActive())
        {
            return HubStatus::Failure(UpgradeError(HubErrorCode::InvalidTransition,
                                                   "A project upgrade operation is already in progress.", root));
        }
        if (m_Worker.joinable())
            m_Worker.join();
        m_Root = std::move(root);
        m_Upgrades.assign(upgrades.begin(), upgrades.end());
        return StartInspection();
    }

    HubStatus HubProjectUpgradeWorkflow::Apply()
    {
        if (const auto owner = RequireOwnerThread("apply"); !owner)
            return owner;
        const auto snapshot = Snapshot();
        if (snapshot->State != HubProjectUpgradeWorkflowState::Ready || snapshot->Interrupted || !snapshot->Plan)
        {
            return HubStatus::Failure(
                UpgradeError(HubErrorCode::InvalidTransition, "The project upgrade is not ready to apply.", m_Root));
        }
        const auto root = m_Root;
        const auto upgrades = m_Upgrades;
        const auto plan = *snapshot->Plan;
        auto apply = m_Services.Apply;
        return StartOperation(HubProjectUpgradeWorkflowState::Applying, HubProjectUpgradeCompletion::Reopen,
                              [root, upgrades, plan, apply = std::move(apply)]
                              {
                                  if (!apply)
                                  {
                                      return HubStatus::Failure(
                                          UpgradeError(HubErrorCode::InvalidData,
                                                       "Project upgrade services are unavailable.", root, {}, false));
                                  }
                                  return apply(root, upgrades, plan);
                              });
    }

    HubStatus HubProjectUpgradeWorkflow::Recover()
    {
        if (const auto owner = RequireOwnerThread("recover"); !owner)
            return owner;
        const auto snapshot = Snapshot();
        if (snapshot->State != HubProjectUpgradeWorkflowState::Ready || !snapshot->Interrupted)
        {
            return HubStatus::Failure(UpgradeError(HubErrorCode::InvalidTransition,
                                                   "The project upgrade has no interrupted operation to recover.",
                                                   m_Root));
        }
        const auto root = m_Root;
        const auto upgrades = m_Upgrades;
        auto recover = m_Services.Recover;
        return StartOperation(HubProjectUpgradeWorkflowState::Recovering, HubProjectUpgradeCompletion::Reopen,
                              [root, upgrades, recover = std::move(recover)]
                              {
                                  if (!recover)
                                  {
                                      return HubStatus::Failure(
                                          UpgradeError(HubErrorCode::InvalidData,
                                                       "Project recovery services are unavailable.", root, {}, false));
                                  }
                                  return recover(root, upgrades);
                              });
    }

    HubStatus HubProjectUpgradeWorkflow::Rollback()
    {
        if (const auto owner = RequireOwnerThread("rollback"); !owner)
            return owner;
        const auto snapshot = Snapshot();
        if (snapshot->State != HubProjectUpgradeWorkflowState::Ready || !snapshot->Interrupted)
        {
            return HubStatus::Failure(UpgradeError(HubErrorCode::InvalidTransition,
                                                   "The project upgrade has no interrupted operation to roll back.",
                                                   m_Root));
        }
        const auto root = m_Root;
        const auto upgrades = m_Upgrades;
        auto rollback = m_Services.Rollback;
        return StartOperation(HubProjectUpgradeWorkflowState::RollingBack, HubProjectUpgradeCompletion::Refresh,
                              [root, upgrades, rollback = std::move(rollback)]
                              {
                                  if (!rollback)
                                  {
                                      return HubStatus::Failure(
                                          UpgradeError(HubErrorCode::InvalidData,
                                                       "Project rollback services are unavailable.", root, {}, false));
                                  }
                                  return rollback(root, upgrades);
                              });
    }

    HubStatus HubProjectUpgradeWorkflow::Retry()
    {
        if (const auto owner = RequireOwnerThread("retry"); !owner)
            return owner;
        if (Snapshot()->State != HubProjectUpgradeWorkflowState::Failed)
        {
            return HubStatus::Failure(
                UpgradeError(HubErrorCode::InvalidTransition, "The project upgrade is not retryable.", m_Root));
        }
        if (m_Worker.joinable())
            m_Worker.join();
        return StartInspection();
    }

    HubStatus HubProjectUpgradeWorkflow::Dismiss()
    {
        if (const auto owner = RequireOwnerThread("dismiss"); !owner)
            return owner;
        if (Snapshot()->IsActive())
        {
            return HubStatus::Failure(UpgradeError(HubErrorCode::InvalidTransition,
                                                   "Wait for the project upgrade operation to finish.", m_Root));
        }
        if (m_Worker.joinable())
            m_Worker.join();
        m_Root.clear();
        m_Upgrades.clear();
        Publish({});
        return HubStatus::Success();
    }

    std::shared_ptr<const HubProjectUpgradeWorkflowSnapshot> HubProjectUpgradeWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    HubStatus HubProjectUpgradeWorkflow::RequireOwnerThread(const std::string_view action) const
    {
        if (std::this_thread::get_id() == m_OwnerThread)
            return HubStatus::Success();
        return HubStatus::Failure(UpgradeError(HubErrorCode::InvalidTransition,
                                               "Project upgrades must be coordinated by their owner thread.", m_Root,
                                               std::string(action)));
    }

    HubStatus HubProjectUpgradeWorkflow::StartOperation(const HubProjectUpgradeWorkflowState state,
                                                        const HubProjectUpgradeCompletion completion,
                                                        Operation operation)
    {
        if (m_Worker.joinable())
            m_Worker.join();
        const auto current = Snapshot();
        Publish({.State = state,
                 .Revision = current->Revision + 1,
                 .Root = m_Root,
                 .Interrupted = current->Interrupted,
                 .Plan = current->Plan});
        try
        {
            m_Worker = std::jthread(
                [this, completion, operation = std::move(operation)]
                {
                    HubStatus status =
                        HubStatus::Failure(UpgradeError(HubErrorCode::WorkerInterrupted,
                                                        "The project upgrade worker did not return a result.", m_Root));
                    try
                    {
                        status = operation();
                    }
                    catch (const std::exception& error)
                    {
                        status = UnexpectedFailure("The project upgrade worker failed unexpectedly.", m_Root, error);
                    }
                    catch (...)
                    {
                        status = UnexpectedFailure("The project upgrade worker failed unexpectedly.", m_Root);
                    }
                    const auto active = Snapshot();
                    if (status)
                    {
                        Publish({.State = HubProjectUpgradeWorkflowState::Completed,
                                 .Revision = active->Revision + 1,
                                 .Root = m_Root,
                                 .Completion = completion});
                    }
                    else
                    {
                        Publish({.State = HubProjectUpgradeWorkflowState::Failed,
                                 .Revision = active->Revision + 1,
                                 .Root = m_Root,
                                 .Failure = status.Error()});
                    }
                });
        }
        catch (const std::exception& error)
        {
            const auto active = Snapshot();
            Publish(
                {.State = HubProjectUpgradeWorkflowState::Failed,
                 .Revision = active->Revision + 1,
                 .Root = m_Root,
                 .Failure = UpgradeError(HubErrorCode::WorkerInterrupted,
                                         "The project upgrade worker could not be started.", m_Root, error.what())});
        }
        return HubStatus::Success();
    }

    HubStatus HubProjectUpgradeWorkflow::StartInspection()
    {
        if (!m_Services.Inspect)
        {
            return HubStatus::Failure(UpgradeError(HubErrorCode::InvalidData,
                                                   "Project upgrade services are unavailable.", m_Root, {}, false));
        }
        const auto current = Snapshot();
        Publish(
            {.State = HubProjectUpgradeWorkflowState::Inspecting, .Revision = current->Revision + 1, .Root = m_Root});
        try
        {
            const auto root = m_Root;
            const auto upgrades = m_Upgrades;
            auto inspect = m_Services.Inspect;
            m_Worker = std::jthread(
                [this, root, upgrades, inspect = std::move(inspect)]
                {
                    auto prepared = HubResult<HubProjectUpgradePreparation>::Failure(
                        UpgradeError(HubErrorCode::WorkerInterrupted,
                                     "The project upgrade inspection did not return a result.", root));
                    try
                    {
                        prepared = inspect(root, upgrades);
                    }
                    catch (const std::exception& error)
                    {
                        prepared = HubResult<HubProjectUpgradePreparation>::Failure(
                            UnexpectedFailure("The project upgrade inspection failed unexpectedly.", root, error)
                                .Error());
                    }
                    catch (...)
                    {
                        prepared = HubResult<HubProjectUpgradePreparation>::Failure(
                            UnexpectedFailure("The project upgrade inspection failed unexpectedly.", root).Error());
                    }
                    const auto active = Snapshot();
                    if (!prepared)
                    {
                        Publish({.State = HubProjectUpgradeWorkflowState::Failed,
                                 .Revision = active->Revision + 1,
                                 .Root = root,
                                 .Failure = prepared.Error()});
                        return;
                    }
                    auto value = std::move(prepared).Value();
                    if (!value.Interrupted && !value.Plan)
                    {
                        Publish({.State = HubProjectUpgradeWorkflowState::Failed,
                                 .Revision = active->Revision + 1,
                                 .Root = root,
                                 .Failure = UpgradeError(HubErrorCode::InvalidData,
                                                         "The project upgrade plan is unavailable.", root, {}, false)});
                        return;
                    }
                    Publish({.State = HubProjectUpgradeWorkflowState::Ready,
                             .Revision = active->Revision + 1,
                             .Root = root,
                             .Interrupted = value.Interrupted,
                             .Plan = std::move(value.Plan)});
                });
        }
        catch (const std::exception& error)
        {
            const auto active = Snapshot();
            Publish({.State = HubProjectUpgradeWorkflowState::Failed,
                     .Revision = active->Revision + 1,
                     .Root = m_Root,
                     .Failure =
                         UpgradeError(HubErrorCode::WorkerInterrupted,
                                      "The project upgrade inspection could not be started.", m_Root, error.what())});
        }
        return HubStatus::Success();
    }

    void HubProjectUpgradeWorkflow::Publish(HubProjectUpgradeWorkflowSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::make_shared<const HubProjectUpgradeWorkflowSnapshot>(std::move(snapshot));
    }

    void HubProjectUpgradeWorkflow::Stop() noexcept
    {
        if (!m_Worker.joinable())
            return;
        try
        {
            m_Worker.join();
        }
        catch (...)
        {
        }
    }
} // namespace KeireHub
