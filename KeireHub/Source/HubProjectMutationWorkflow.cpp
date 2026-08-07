#include "KeireHub/HubProjectMutationWorkflow.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError MutationError(const HubErrorCode code, std::string message, std::string item,
                                             std::string details = {}, const bool retryable = false)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] HubProjectMutationSnapshot
        FailureSnapshot(const std::uint64_t operationId, std::string sourceProjectId, std::filesystem::path destination,
                        HubError error, const std::uint64_t copiedBytes = 0, const std::size_t copiedEntries = 0)
        {
            return {.OperationId = operationId,
                    .State = HubProjectMutationState::Failed,
                    .SourceProjectId = std::move(sourceProjectId),
                    .Destination = std::move(destination),
                    .CopiedBytes = copiedBytes,
                    .CopiedEntries = copiedEntries,
                    .Failure = std::move(error)};
        }
    } // namespace

    bool HubProjectMutationSnapshot::IsActive() const noexcept
    {
        return State == HubProjectMutationState::Duplicating || State == HubProjectMutationState::Cancelling;
    }

    bool HubProjectMutationSnapshot::IsTerminal() const noexcept
    {
        return State == HubProjectMutationState::Completed || State == HubProjectMutationState::Failed ||
               State == HubProjectMutationState::Cancelled;
    }

    HubProjectMutationWorkflow::HubProjectMutationWorkflow(HubProjectMutationServices services)
        : m_Services(std::move(services)), m_OwnerThread(std::this_thread::get_id()),
          m_Snapshot(std::make_shared<const HubProjectMutationSnapshot>())
    {
    }

    HubProjectMutationWorkflow::~HubProjectMutationWorkflow() { DiscardOnShutdown(); }

    HubResult<std::uint64_t> HubProjectMutationWorkflow::StartDuplicate(std::string sourceProjectId,
                                                                        std::filesystem::path destination,
                                                                        std::string displayName)
    {
        if (const auto owner = RequireOwnerThread("start"); !owner)
            return HubResult<std::uint64_t>::Failure(owner.Error());
        if (m_Future.valid())
        {
            return HubResult<std::uint64_t>::Failure(MutationError(HubErrorCode::InvalidTransition,
                                                                   "A project mutation is already active.",
                                                                   Snapshot()->SourceProjectId, {}, true));
        }

        const auto operationId = m_NextOperationId;
        m_NextOperationId = m_NextOperationId == std::numeric_limits<std::uint64_t>::max() ? 1 : m_NextOperationId + 1;
        if (!m_Services.PrepareDuplicate || !m_Services.StageDuplicate || !m_Services.CommitDuplicate ||
            !m_Services.DiscardDuplicate)
        {
            auto error =
                MutationError(HubErrorCode::InvalidData, "Project mutation services are unavailable.", sourceProjectId);
            Publish(FailureSnapshot(operationId, sourceProjectId, destination, error));
            return HubResult<std::uint64_t>::Success(operationId);
        }

        HubResult<ProjectDuplicatePlan> prepared = HubResult<ProjectDuplicatePlan>::Failure(
            MutationError(HubErrorCode::InvalidData, "The project duplicate could not be prepared.", sourceProjectId));
        try
        {
            prepared = m_Services.PrepareDuplicate(sourceProjectId, destination, std::move(displayName));
        }
        catch (const std::exception& error)
        {
            prepared = HubResult<ProjectDuplicatePlan>::Failure(
                MutationError(HubErrorCode::InvalidData, "The project duplicate could not be prepared.",
                              sourceProjectId, error.what()));
        }
        catch (...)
        {
            prepared = HubResult<ProjectDuplicatePlan>::Failure(
                MutationError(HubErrorCode::InvalidData, "The project duplicate could not be prepared.",
                              sourceProjectId, "The preparation service failed with a non-standard exception."));
        }
        if (!prepared)
        {
            Publish(FailureSnapshot(operationId, std::move(sourceProjectId), std::move(destination), prepared.Error()));
            return HubResult<std::uint64_t>::Success(operationId);
        }

        m_CancelRequested.store(false, std::memory_order_release);
        m_CopiedBytes.store(0, std::memory_order_release);
        m_CopiedEntries.store(0, std::memory_order_release);
        Publish({.OperationId = operationId,
                 .State = HubProjectMutationState::Duplicating,
                 .SourceProjectId = sourceProjectId,
                 .Destination = destination});
        try
        {
            auto stage = m_Services.StageDuplicate;
            m_Future = std::async(
                std::launch::async,
                [this, stage = std::move(stage), plan = std::move(prepared).Value()]() mutable
                {
                    return stage(std::move(plan),
                                 {.IsCancelled = [this] { return m_CancelRequested.load(std::memory_order_acquire); },
                                  .ReportProgress = [this](const std::uint64_t bytes, const std::size_t entries)
                                  { CaptureProgress(bytes, entries); }});
                });
        }
        catch (const std::exception& error)
        {
            auto failure =
                MutationError(HubErrorCode::WorkerInterrupted, "The project duplication worker could not be started.",
                              sourceProjectId, error.what(), true);
            Publish(FailureSnapshot(operationId, std::move(sourceProjectId), std::move(destination), failure));
        }
        return HubResult<std::uint64_t>::Success(operationId);
    }

    HubResult<bool> HubProjectMutationWorkflow::Poll()
    {
        if (const auto owner = RequireOwnerThread("poll"); !owner)
            return HubResult<bool>::Failure(owner.Error());
        if (!m_Future.valid())
            return HubResult<bool>::Success(false);

        const auto operationId = Snapshot()->OperationId;
        PublishCapturedProgress(operationId);
        if (m_Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return HubResult<bool>::Success(false);

        const auto active = Snapshot();
        StageResult staged = HubResult<ProjectDuplicateStagedResult>::Failure(
            MutationError(HubErrorCode::WorkerInterrupted, "The project duplication worker did not return a result.",
                          active->SourceProjectId, {}, true));
        try
        {
            staged = m_Future.get();
        }
        catch (const std::exception& error)
        {
            staged = HubResult<ProjectDuplicateStagedResult>::Failure(
                MutationError(HubErrorCode::WorkerInterrupted, "The project duplication worker failed.",
                              active->SourceProjectId, error.what(), true));
        }
        catch (...)
        {
            staged = HubResult<ProjectDuplicateStagedResult>::Failure(
                MutationError(HubErrorCode::WorkerInterrupted, "The project duplication worker failed.",
                              active->SourceProjectId, "The worker failed with a non-standard exception.", true));
        }
        if (!staged)
        {
            Publish(FailureSnapshot(active->OperationId, active->SourceProjectId, active->Destination, staged.Error(),
                                    active->CopiedBytes, active->CopiedEntries));
            return HubResult<bool>::Failure(staged.Error());
        }

        auto stagedValue = std::move(staged).Value();
        if (m_CancelRequested.load(std::memory_order_acquire) ||
            stagedValue.State == ProjectDuplicateStageState::Cancelled)
        {
            HubStatus discarded = HubStatus::Success();
            try
            {
                discarded = m_Services.DiscardDuplicate(stagedValue);
            }
            catch (const std::exception& error)
            {
                discarded = HubStatus::Failure(MutationError(HubErrorCode::IoWrite,
                                                             "The cancelled project duplicate could not be cleaned up.",
                                                             active->SourceProjectId, error.what(), true));
            }
            catch (...)
            {
                discarded = HubStatus::Failure(MutationError(
                    HubErrorCode::IoWrite, "The cancelled project duplicate could not be cleaned up.",
                    active->SourceProjectId, "The cleanup service failed with a non-standard exception.", true));
            }
            if (!discarded)
            {
                Publish(FailureSnapshot(active->OperationId, active->SourceProjectId, active->Destination,
                                        discarded.Error(), active->CopiedBytes, active->CopiedEntries));
                return HubResult<bool>::Failure(discarded.Error());
            }
            Publish({.OperationId = active->OperationId,
                     .State = HubProjectMutationState::Cancelled,
                     .SourceProjectId = active->SourceProjectId,
                     .Destination = active->Destination,
                     .CopiedBytes = std::max(active->CopiedBytes, stagedValue.CopiedBytes),
                     .CopiedEntries = std::max(active->CopiedEntries, stagedValue.CopiedEntries)});
            return HubResult<bool>::Success(true);
        }

        auto committed = HubResult<ProjectDuplicateResult>::Failure(
            MutationError(HubErrorCode::InvalidData, "The staged project duplicate could not be published.",
                          active->SourceProjectId));
        try
        {
            committed = m_Services.CommitDuplicate(stagedValue);
        }
        catch (const std::exception& error)
        {
            committed = HubResult<ProjectDuplicateResult>::Failure(
                MutationError(HubErrorCode::IoWrite, "The staged project duplicate could not be published.",
                              active->SourceProjectId, error.what(), true));
        }
        catch (...)
        {
            committed = HubResult<ProjectDuplicateResult>::Failure(MutationError(
                HubErrorCode::IoWrite, "The staged project duplicate could not be published.", active->SourceProjectId,
                "The commit service failed with a non-standard exception.", true));
        }
        if (!committed)
        {
            auto error = committed.Error();
            try
            {
                const auto discarded = m_Services.DiscardDuplicate(stagedValue);
                if (!discarded && !discarded.Error().TechnicalDetails.empty())
                    error.TechnicalDetails += " Cleanup: " + discarded.Error().TechnicalDetails;
            }
            catch (const std::exception& cleanupError)
            {
                error.TechnicalDetails += " Cleanup: " + std::string(cleanupError.what());
            }
            catch (...)
            {
                error.TechnicalDetails += " Cleanup failed with a non-standard exception.";
            }
            Publish(FailureSnapshot(active->OperationId, active->SourceProjectId, active->Destination, error,
                                    std::max(active->CopiedBytes, stagedValue.CopiedBytes),
                                    std::max(active->CopiedEntries, stagedValue.CopiedEntries)));
            return HubResult<bool>::Failure(error);
        }
        Publish({.OperationId = active->OperationId,
                 .State = HubProjectMutationState::Completed,
                 .SourceProjectId = active->SourceProjectId,
                 .Destination = active->Destination,
                 .CopiedBytes = stagedValue.CopiedBytes,
                 .CopiedEntries = stagedValue.CopiedEntries,
                 .Result = std::move(committed).Value()});
        return HubResult<bool>::Success(true);
    }

    HubStatus HubProjectMutationWorkflow::Cancel(const std::uint64_t operationId)
    {
        if (const auto owner = RequireOwnerThread("cancel"); !owner)
            return owner;
        const auto snapshot = Snapshot();
        if (snapshot->OperationId != operationId || !snapshot->IsActive())
        {
            return HubStatus::Failure(MutationError(HubErrorCode::InvalidTransition,
                                                    "The project mutation is not cancellable.",
                                                    snapshot->SourceProjectId));
        }
        m_CancelRequested.store(true, std::memory_order_release);
        if (snapshot->State != HubProjectMutationState::Cancelling)
        {
            auto cancelling = *snapshot;
            cancelling.State = HubProjectMutationState::Cancelling;
            Publish(std::move(cancelling));
        }
        return HubStatus::Success();
    }

    std::shared_ptr<const HubProjectMutationSnapshot> HubProjectMutationWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    HubStatus HubProjectMutationWorkflow::RequireOwnerThread(const std::string_view action) const
    {
        if (std::this_thread::get_id() == m_OwnerThread)
            return HubStatus::Success();
        return HubStatus::Failure(MutationError(HubErrorCode::InvalidTransition,
                                                "Project mutations must be coordinated by their owner thread.",
                                                std::string(action)));
    }

    void HubProjectMutationWorkflow::Publish(HubProjectMutationSnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::make_shared<const HubProjectMutationSnapshot>(std::move(snapshot));
    }

    void HubProjectMutationWorkflow::CaptureProgress(const std::uint64_t bytes, const std::size_t entries) noexcept
    {
        m_CopiedBytes.store(bytes, std::memory_order_release);
        m_CopiedEntries.store(entries, std::memory_order_release);
    }

    void HubProjectMutationWorkflow::PublishCapturedProgress(const std::uint64_t operationId)
    {
        const auto snapshot = Snapshot();
        if (snapshot->OperationId != operationId || !snapshot->IsActive())
            return;
        const auto bytes = m_CopiedBytes.load(std::memory_order_acquire);
        const auto entries = m_CopiedEntries.load(std::memory_order_acquire);
        if (snapshot->CopiedBytes == bytes && snapshot->CopiedEntries == entries)
            return;
        auto progress = *snapshot;
        progress.State = m_CancelRequested.load(std::memory_order_acquire) ? HubProjectMutationState::Cancelling
                                                                           : HubProjectMutationState::Duplicating;
        progress.CopiedBytes = bytes;
        progress.CopiedEntries = entries;
        Publish(std::move(progress));
    }

    void HubProjectMutationWorkflow::DiscardOnShutdown() noexcept
    {
        m_CancelRequested.store(true, std::memory_order_release);
        if (!m_Future.valid())
            return;
        try
        {
            auto staged = m_Future.get();
            if (staged && m_Services.DiscardDuplicate)
                (void)m_Services.DiscardDuplicate(staged.Value());
        }
        catch (...)
        {
        }
    }
} // namespace KeireHub
