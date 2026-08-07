#include "KeireHub/HubUpdateHandoffWorkflow.h"

#include <exception>
#include <utility>

namespace KeireHub
{
    HubUpdateHandoffWorkflow::HubUpdateHandoffWorkflow()
        : m_Snapshot(std::make_shared<const HubUpdateHandoffSnapshot>())
    {
    }

    HubUpdateHandoffWorkflow::~HubUpdateHandoffWorkflow() { Stop(); }

    HubStatus HubUpdateHandoffWorkflow::Start(HubUpdateManager& manager, HubUpdateRequest request,
                                              HubUpdateManager::PlatformSignatureVerifier signatureVerifier,
                                              HubUpdateManager::InstallerLauncher launcher)
    {
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Snapshot->State == HubUpdateHandoffState::Verifying ||
                m_Snapshot->State == HubUpdateHandoffState::Launched)
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidTransition,
                                           .Message = "The Hub installer handoff is already in progress.",
                                           .AffectedItem = request.PackageId,
                                           .TechnicalDetails = {},
                                           .LogReference = {}});
            }
        }
        if (m_Worker.joinable())
            m_Worker.join();
        Publish(HubUpdateHandoffState::Verifying);
        m_Worker = std::jthread(
            [this, &manager, request = std::move(request), signatureVerifier = std::move(signatureVerifier),
             launcher = std::move(launcher)]
            {
                try
                {
                    auto status = manager.BeginInstallerHandoff(request, signatureVerifier, launcher);
                    if (status)
                        Publish(HubUpdateHandoffState::Launched);
                    else
                        Publish(HubUpdateHandoffState::Failed, status.Error());
                }
                catch (const std::exception& error)
                {
                    Publish(HubUpdateHandoffState::Failed,
                            HubError{.Code = HubErrorCode::WorkerInterrupted,
                                     .Message = "The native Hub installer handoff failed unexpectedly.",
                                     .Retryable = true,
                                     .AffectedItem = request.PackageId,
                                     .TechnicalDetails = error.what(),
                                     .LogReference = {}});
                }
            });
        return HubStatus::Success();
    }

    void HubUpdateHandoffWorkflow::Stop() noexcept
    {
        if (!m_Worker.joinable())
            return;
        m_Worker.request_stop();
        m_Worker.join();
    }

    std::shared_ptr<const HubUpdateHandoffSnapshot> HubUpdateHandoffWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    std::optional<HubUpdateHandoffSnapshot> HubUpdateHandoffWorkflow::TakeCompletion()
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Snapshot->Revision == m_ConsumedRevision || (m_Snapshot->State != HubUpdateHandoffState::Launched &&
                                                           m_Snapshot->State != HubUpdateHandoffState::Failed))
        {
            return std::nullopt;
        }
        m_ConsumedRevision = m_Snapshot->Revision;
        return *m_Snapshot;
    }

    void HubUpdateHandoffWorkflow::Publish(const HubUpdateHandoffState state, std::optional<HubError> failure)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::make_shared<const HubUpdateHandoffSnapshot>(HubUpdateHandoffSnapshot{
            .State = state, .Revision = m_Snapshot->Revision + 1, .Failure = std::move(failure)});
    }
} // namespace KeireHub
