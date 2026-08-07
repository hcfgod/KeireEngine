#include "KeireHub/HubBuildSupportInventoryWorkflow.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <ranges>
#include <string>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError InventoryError(const HubErrorCode code, std::string message, std::string details = {},
                                              const bool retryable = true)
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .Retryable = retryable,
                    .AffectedItem = "build-support",
                    .TechnicalDetails = std::move(details)};
        }

        void SortInventory(std::vector<BuildSupportComponent>& inventory)
        {
            std::ranges::sort(inventory,
                              [](const auto& left, const auto& right)
                              {
                                  if (left.EngineVersion != right.EngineVersion)
                                      return left.EngineVersion < right.EngineVersion;
                                  if (left.Platform != right.Platform)
                                      return left.Platform < right.Platform;
                                  if (left.Architecture != right.Architecture)
                                      return left.Architecture < right.Architecture;
                                  return left.Id < right.Id;
                              });
        }
    } // namespace

    HubBuildSupportInventoryWorkflow::HubBuildSupportInventoryWorkflow(HubBuildSupportInventoryServices services)
        : m_Services(std::move(services)), m_OwnerThread(std::this_thread::get_id()),
          m_Snapshot(std::make_shared<const HubBuildSupportInventorySnapshot>(HubBuildSupportInventorySnapshot{
              .Components = std::make_shared<const std::vector<BuildSupportComponent>>()}))
    {
    }

    HubBuildSupportInventoryWorkflow::~HubBuildSupportInventoryWorkflow() { Stop(); }

    HubStatus HubBuildSupportInventoryWorkflow::Start()
    {
        if (const auto owner = RequireOwnerThread("start"); !owner)
            return owner;
        if (m_Future.valid() || Snapshot()->IsLoading())
        {
            return HubStatus::Failure(
                InventoryError(HubErrorCode::InvalidTransition, "Build Support inventory is already being refreshed."));
        }
        if (!m_Services.Load)
        {
            return HubStatus::Failure(InventoryError(HubErrorCode::InvalidData,
                                                     "Build Support inventory services are unavailable.", {}, false));
        }

        const auto previous = Snapshot();
        Publish({.Revision = previous->Revision + 1,
                 .State = HubBuildSupportInventoryState::Loading,
                 .Components = previous->Components});
        try
        {
            auto load = m_Services.Load;
            m_Future = std::async(std::launch::async, [load = std::move(load)] { return load(); });
        }
        catch (const std::exception& error)
        {
            const auto loading = Snapshot();
            Publish(
                {.Revision = loading->Revision + 1,
                 .State = HubBuildSupportInventoryState::Failed,
                 .Components = loading->Components,
                 .Failure = InventoryError(HubErrorCode::WorkerInterrupted,
                                           "The Build Support inventory worker could not be started.", error.what())});
        }
        catch (...)
        {
            const auto loading = Snapshot();
            Publish({.Revision = loading->Revision + 1,
                     .State = HubBuildSupportInventoryState::Failed,
                     .Components = loading->Components,
                     .Failure = InventoryError(HubErrorCode::WorkerInterrupted,
                                               "The Build Support inventory worker could not be started.",
                                               "The worker launcher failed with a non-standard exception.")});
        }
        return HubStatus::Success();
    }

    HubStatus HubBuildSupportInventoryWorkflow::RequestRefresh()
    {
        if (const auto owner = RequireOwnerThread("request refresh"); !owner)
            return owner;
        if (m_Future.valid() || Snapshot()->IsLoading())
        {
            m_RefreshPending = true;
            return HubStatus::Success();
        }
        return Start();
    }

    HubResult<bool> HubBuildSupportInventoryWorkflow::Poll()
    {
        if (const auto owner = RequireOwnerThread("poll"); !owner)
            return HubResult<bool>::Failure(owner.Error());
        if (!m_Future.valid() || m_Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return HubResult<bool>::Success(false);

        const auto loading = Snapshot();
        LoadResult loaded = LoadResult::Failure(InventoryError(
            HubErrorCode::WorkerInterrupted, "The Build Support inventory worker did not return a result."));
        try
        {
            loaded = m_Future.get();
        }
        catch (const std::exception& error)
        {
            loaded = LoadResult::Failure(InventoryError(
                HubErrorCode::WorkerInterrupted, "Build Support inventory could not be refreshed.", error.what()));
        }
        catch (...)
        {
            loaded = LoadResult::Failure(InventoryError(HubErrorCode::WorkerInterrupted,
                                                        "Build Support inventory could not be refreshed.",
                                                        "The inventory worker failed with a non-standard exception."));
        }

        if (!loaded)
        {
            Publish({.Revision = loading->Revision + 1,
                     .State = HubBuildSupportInventoryState::Failed,
                     .Components = loading->Components,
                     .Failure = loaded.Error()});
        }
        else
        {
            auto inventory = std::move(loaded).Value();
            SortInventory(inventory);
            Publish({.Revision = loading->Revision + 1,
                     .State = HubBuildSupportInventoryState::Ready,
                     .Components = std::make_shared<const std::vector<BuildSupportComponent>>(std::move(inventory))});
        }
        if (std::exchange(m_RefreshPending, false))
        {
            if (const auto restarted = Start(); !restarted)
                return HubResult<bool>::Failure(restarted.Error());
        }
        return HubResult<bool>::Success(true);
    }

    std::shared_ptr<const HubBuildSupportInventorySnapshot> HubBuildSupportInventoryWorkflow::Snapshot() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Snapshot;
    }

    void HubBuildSupportInventoryWorkflow::Stop() noexcept
    {
        m_RefreshPending = false;
        if (!m_Future.valid())
            return;
        try
        {
            m_Future.wait();
        }
        catch (...)
        {
        }
    }

    HubStatus HubBuildSupportInventoryWorkflow::RequireOwnerThread(const std::string_view action) const
    {
        if (std::this_thread::get_id() == m_OwnerThread)
            return HubStatus::Success();
        return HubStatus::Failure(InventoryError(HubErrorCode::InvalidTransition,
                                                 "Build Support inventory must be coordinated by its owner thread.",
                                                 std::string(action), false));
    }

    void HubBuildSupportInventoryWorkflow::Publish(HubBuildSupportInventorySnapshot snapshot)
    {
        std::scoped_lock lock(m_Mutex);
        m_Snapshot = std::make_shared<const HubBuildSupportInventorySnapshot>(std::move(snapshot));
    }
} // namespace KeireHub
