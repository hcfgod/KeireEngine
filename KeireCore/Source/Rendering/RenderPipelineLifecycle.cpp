#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    bool RenderSharedState::WaitForRecoveryAtOwnerBoundary(const std::function<bool()>& pumpWindowEvents)
    {
        RethrowTerminalFailure();
        std::unique_lock lock(RenderQueueMutex);
        const auto state = DeviceLifecycle.load(std::memory_order_acquire);
        if (state != RenderDeviceState::RecoveryPending && state != RenderDeviceState::Recovering)
            return false;
        RecoveryOwnerBoundary = true;
        FramesRetired.notify_all();
        const auto recoveryFinished = [this]
        {
            const auto current = DeviceLifecycle.load(std::memory_order_acquire);
            return current != RenderDeviceState::RecoveryPending && current != RenderDeviceState::Recovering;
        };
        while (!recoveryFinished())
        {
            if (WindowRecreationRequested && !WindowRecreationCompleted)
            {
                WindowRecreationRequested = false;
                lock.unlock();
                SDL_Window* replacement = nullptr;
                std::exception_ptr failure;
                try
                {
                    replacement = WindowSystemInternalAccess::RecreateNativeWindow(*Windows, Window->Id());
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
                lock.lock();
                if (replacement)
                    NativeWindow = replacement;
                WindowRecreationFailure = std::move(failure);
                WindowRecreationCompleted = true;
                FramesRetired.notify_all();
                continue;
            }
            if (!pumpWindowEvents)
            {
                FramesRetired.wait(lock, [this, &recoveryFinished]
                                   { return recoveryFinished() || WindowRecreationRequested; });
                continue;
            }
            if (FramesRetired.wait_for(lock, std::chrono::milliseconds(5), recoveryFinished))
                break;
            lock.unlock();
            const bool keepWaiting = pumpWindowEvents();
            lock.lock();
            if (!keepWaiting)
                return true;
        }
        lock.unlock();
        RethrowTerminalFailure();
        return true;
    }

    SDL_Window* RenderSharedState::RequestWindowRecreationAtOwnerBoundary()
    {
        RequireRenderThread("RequestWindowRecreationAtOwnerBoundary");
        std::unique_lock lock(RenderQueueMutex);
        WindowRecreationCompleted = false;
        WindowRecreationFailure = nullptr;
        WindowRecreationRequested = true;
        FramesRetired.notify_all();
        FramesRetired.wait(lock,
                           [this]
                           {
                               return WindowRecreationCompleted ||
                                      DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Closing;
                           });
        if (DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Closing)
            throw std::logic_error("Window recreation was cancelled because the renderer is closing.");
        if (WindowRecreationFailure)
            std::rethrow_exception(WindowRecreationFailure);
        if (!NativeWindow)
            throw std::runtime_error("Window recreation completed without a native window.");
        return NativeWindow;
    }

    void RenderSharedState::EnqueueFrame(const std::shared_ptr<RenderFramePacket>& frame,
                                         const std::function<void()>& capture)
    {
        const auto admissionStarted = std::chrono::steady_clock::now();
        {
            std::unique_lock lock(RenderQueueMutex);
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            const bool observedAdmissionWait =
                AvailableFrameSlots.empty() && !StopRenderQueue &&
                DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Running;
            if (observedAdmissionWait)
            {
                ++FrameAdmissionWaiters;
                FramesRetired.notify_all();
            }
#endif
            FramesRetired.wait(lock,
                               [this]
                               {
                                   return StopRenderQueue ||
                                          DeviceLifecycle.load(std::memory_order_acquire) !=
                                              RenderDeviceState::Running ||
                                          !AvailableFrameSlots.empty();
                               });
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            if (observedAdmissionWait)
                --FrameAdmissionWaiters;
#endif
            if (StopRenderQueue)
                throw std::logic_error("Renderer submission queue is closed.");
            const auto lifecycle = DeviceLifecycle.load(std::memory_order_acquire);
            if (lifecycle == RenderDeviceState::RecoveryPending || lifecycle == RenderDeviceState::Recovering)
                throw RenderRecoveryBoundaryRequired();
            if (lifecycle == RenderDeviceState::Failed)
            {
                lock.unlock();
                RethrowTerminalFailure();
                throw std::runtime_error("Renderer failed without a terminal diagnostic.");
            }
            if (lifecycle != RenderDeviceState::Running)
                throw std::logic_error("Renderer is closing and cannot accept another frame.");
            frame->FrameSlot = AvailableFrameSlots.front();
            AvailableFrameSlots.pop_front();
            frame->Timeline.AdmissionWaitMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - admissionStarted).count();
        }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
        if (InjectRecoveryAtAdmissionBarrier.exchange(false, std::memory_order_acq_rel))
        {
            std::scoped_lock lock(RenderQueueMutex);
            (void)TryBeginDeviceRecovery(DeviceLifecycle);
        }
#endif
        {
            std::unique_lock lock(RenderQueueMutex);
            const auto lifecycle = DeviceLifecycle.load(std::memory_order_acquire);
            if (lifecycle != RenderDeviceState::Running)
            {
                AvailableFrameSlots.push_back(frame->FrameSlot);
                frame->FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
                lock.unlock();
                FramesRetired.notify_all();
                if (lifecycle == RenderDeviceState::RecoveryPending || lifecycle == RenderDeviceState::Recovering)
                    throw RenderRecoveryBoundaryRequired();
                if (lifecycle == RenderDeviceState::Failed)
                    RethrowTerminalFailure();
                throw std::logic_error("Renderer is closing and cannot capture another frame.");
            }
        }

        try
        {
            capture();
        }
        catch (...)
        {
            {
                std::scoped_lock lock(RenderQueueMutex);
                if (frame->FrameSlot != (std::numeric_limits<std::uint32_t>::max)())
                {
                    AvailableFrameSlots.push_back(frame->FrameSlot);
                    frame->FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
                }
            }
            FramesRetired.notify_all();
            throw;
        }

        bool executeSynchronously = false;
        try
        {
            std::unique_lock lock(RenderQueueMutex);
            const auto lifecycle = DeviceLifecycle.load(std::memory_order_acquire);
            if (lifecycle != RenderDeviceState::Running)
            {
                AvailableFrameSlots.push_back(frame->FrameSlot);
                frame->FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
                lock.unlock();
                FramesRetired.notify_all();
                if (lifecycle == RenderDeviceState::RecoveryPending || lifecycle == RenderDeviceState::Recovering)
                    throw RenderRecoveryBoundaryRequired();
                if (lifecycle == RenderDeviceState::Failed)
                {
                    RethrowTerminalFailure();
                    throw std::runtime_error("Renderer failed without a terminal diagnostic.");
                }
                throw std::logic_error("Renderer is closing and cannot accept another frame.");
            }
            frame->AcceptedAt = std::chrono::steady_clock::now();
            const auto outstanding = OutstandingFrames.fetch_add(1, std::memory_order_acq_rel) + 1U;
            frame->Timeline.OutstandingAtAdmission = outstanding;
            auto highWater = OutstandingHighWaterMark.load(std::memory_order_relaxed);
            while (highWater < outstanding &&
                   !OutstandingHighWaterMark.compare_exchange_weak(highWater, outstanding, std::memory_order_relaxed))
            {
            }
            AcceptedFrameCount.fetch_add(1, std::memory_order_relaxed);
            LastAcceptedFrameId.store(frame->Id, std::memory_order_relaxed);
            if (RenderThread.joinable())
            {
                RenderQueue.push_back({[this, frame] { ExecuteAcceptedFrame(frame); }, frame->Id, frame});
                const auto queueDepth = static_cast<std::uint32_t>(RenderQueue.size());
                auto queueHighWater = RenderQueueHighWaterMark.load(std::memory_order_relaxed);
                while (queueHighWater < queueDepth && !RenderQueueHighWaterMark.compare_exchange_weak(
                                                          queueHighWater, queueDepth, std::memory_order_relaxed))
                {
                }
            }
            else
            {
                executeSynchronously = true;
            }
        }
        catch (...)
        {
            throw;
        }
        if (!executeSynchronously)
        {
            RenderQueueReady.notify_one();
            return;
        }
        ExecuteAcceptedFrame(frame);
        RethrowTerminalFailure();
    }

    void RenderSharedState::Flush()
    {
        RequireOwner("Flush");
        if (std::this_thread::get_id() == RenderThreadId)
            throw std::logic_error("RenderSystem::Flush cannot run on the render thread.");
        std::unique_lock lock(RenderQueueMutex);
        FramesRetired.wait(lock,
                           [this]
                           {
                               return OutstandingFrames.load(std::memory_order_acquire) == 0U ||
                                      DeviceLifecycle.load(std::memory_order_acquire) != RenderDeviceState::Running;
                           });
        const auto lifecycle = DeviceLifecycle.load(std::memory_order_acquire);
        lock.unlock();
        {
            std::scoped_lock publicationLock(PublicationMutex);
        }
        if (lifecycle == RenderDeviceState::RecoveryPending || lifecycle == RenderDeviceState::Recovering)
            throw RenderRecoveryBoundaryRequired();
        RethrowTerminalFailure();
        if (lifecycle == RenderDeviceState::Closing || lifecycle == RenderDeviceState::Closed)
            throw std::logic_error("RenderSystem::Flush was interrupted because the renderer is closing.");
    }

    void RenderSharedState::RethrowTerminalFailure() const
    {
        std::exception_ptr failure;
        {
            std::scoped_lock lock(FailureMutex);
            failure = TerminalFailure;
        }
        if (failure)
            std::rethrow_exception(failure);
    }

    void RenderSharedState::RecordTerminalFailure(std::exception_ptr failure,
                                                  const std::shared_ptr<RenderFramePacket>& activeFrame) noexcept
    {
        if (!failure)
            return;
        bool firstFailure = false;
        {
            std::scoped_lock lock(FailureMutex);
            if (!TerminalFailure)
            {
                TerminalFailure = std::move(failure);
                firstFailure = true;
            }
        }
        if (firstFailure)
            PublishTerminalDeviceFailure(DeviceLifecycle);
        // Latch the terminal state before returning the failed frame's slot, then publish that frame before any
        // later unstarted packets are cancelled. This preserves frame-id order without reopening admission.
        if (activeFrame)
            CompleteFrame(activeFrame, true);
        if (firstFailure)
            CancelQueuedFrames();
        FramesRetired.notify_all();
        RenderQueueReady.notify_all();
    }

    void RenderSharedState::CancelQueuedFrames() noexcept
    {
        for (;;)
        {
            std::shared_ptr<RenderFramePacket> cancelled;
            {
                std::scoped_lock lock(RenderQueueMutex);
                const auto iterator =
                    std::ranges::find_if(RenderQueue, [](const RenderQueueItem& task) { return task.Frame != 0U; });
                if (iterator == RenderQueue.end())
                    break;
                cancelled = std::move(iterator->Packet);
                RenderQueue.erase(iterator);
            }
            if (cancelled)
            {
                try
                {
                    CompleteFrame(cancelled, true);
                }
                catch (...)
                {
                }
            }
        }
        RenderQueueSpace.notify_all();
    }

    void RenderSharedState::CompleteFrame(const std::shared_ptr<RenderFramePacket>& frame,
                                          const bool cancelled) noexcept
    {
        if (!frame)
            return;
        if (frame->CompletionPublished.exchange(true, std::memory_order_acq_rel))
            return;
        frame->RetiredAt = std::chrono::steady_clock::now();
        frame->Timeline.Cancelled = cancelled;
        frame->Timeline.RetriedAfterDeviceLoss = frame->RetriedAfterDeviceLoss;
        frame->Timeline.Presented = frame->PresentedAt != std::chrono::steady_clock::time_point{};
        if (frame->SubmittedAt != std::chrono::steady_clock::time_point{})
        {
            frame->Timeline.GpuRetirementMilliseconds =
                std::chrono::duration<float, std::milli>(frame->RetiredAt - frame->SubmittedAt).count();
        }
        if (frame->PresentedAt != std::chrono::steady_clock::time_point{})
        {
            frame->Timeline.SubmitToPresentMilliseconds =
                std::chrono::duration<float, std::milli>(frame->PresentedAt - frame->SubmittedAt).count();
        }
        if (cancelled)
            CancelledFrameCount.fetch_add(1, std::memory_order_relaxed);
        else
        {
            RetiredFrameCount.fetch_add(1, std::memory_order_relaxed);
            LastRetiredFrameId.store(frame->Id, std::memory_order_relaxed);
            ++RetiredFramesSinceRecovery;
            if (RecoveryAttemptsUsed.load(std::memory_order_acquire) != 0U &&
                LastRecoveryCompletedAt != std::chrono::steady_clock::time_point{} &&
                RetiredFramesSinceRecovery >= 120U &&
                frame->RetiredAt - LastRecoveryCompletedAt >= std::chrono::seconds(60))
            {
                RecoveryAttemptsUsed.store(0U, std::memory_order_release);
                RetiredFramesSinceRecovery = 0;
                LastRecoveryCompletedAt = {};
            }
        }
        {
            std::scoped_lock publicationLock(PublicationMutex);
            try
            {
                PublishedTimelines.push_back(frame->Timeline);
                constexpr std::size_t maximumPublishedTimelines = 256U;
                while (PublishedTimelines.size() > maximumPublishedTimelines)
                    PublishedTimelines.pop_front();
            }
            catch (...)
            {
            }
            auto previous = OutstandingFrames.load(std::memory_order_acquire);
            while (previous != 0U && !OutstandingFrames.compare_exchange_weak(
                                         previous, previous - 1U, std::memory_order_acq_rel, std::memory_order_acquire))
            {
            }
            RefreshStatisticsCounters();
            PublishedStatistics = Statistics;
        }
        if (frame->FrameSlot != (std::numeric_limits<std::uint32_t>::max)())
        {
            std::scoped_lock lock(RenderQueueMutex);
            AvailableFrameSlots.push_back(frame->FrameSlot);
            frame->FrameSlot = (std::numeric_limits<std::uint32_t>::max)();
        }
        FramesRetired.notify_all();
        RenderQueueSpace.notify_all();
    }

    void RenderSharedState::RefreshStatisticsCounters() noexcept
    {
        Statistics.OutstandingFrames = OutstandingFrames.load(std::memory_order_relaxed);
        Statistics.FramesInFlightHighWaterMark = OutstandingHighWaterMark.load(std::memory_order_relaxed);
        Statistics.AcceptedFrames = AcceptedFrameCount.load(std::memory_order_relaxed);
        Statistics.PresentedFrames = PresentedFrameCount.load(std::memory_order_relaxed);
        Statistics.RetiredFrames = RetiredFrameCount.load(std::memory_order_relaxed);
        Statistics.CancelledFrames = CancelledFrameCount.load(std::memory_order_relaxed);
        Statistics.LastAcceptedFrame = LastAcceptedFrameId.load(std::memory_order_relaxed);
        Statistics.LastPresentedFrame = LastPresentedFrameId.load(std::memory_order_relaxed);
        Statistics.LastRetiredFrame = LastRetiredFrameId.load(std::memory_order_relaxed);
        Statistics.RendererQueueHighWaterMark = RenderQueueHighWaterMark.load(std::memory_order_relaxed);
    }

    void RenderSharedState::PublishStatistics() noexcept
    {
        RefreshStatisticsCounters();
        std::scoped_lock lock(PublicationMutex);
        PublishedStatistics = Statistics;
    }

    GpuDeviceLossDiagnostic RenderSharedState::DeviceLossDiagnostic(std::string operation, std::string detail) const
    {
        RenderDeviceIdentity identity;
        {
            std::scoped_lock lock(DeviceIdentityMutex);
            identity = DeviceIdentitySnapshot;
        }
        return {.Operation = std::move(operation),
                .Backend = identity.Backend.empty() ? DeviceDriver : std::move(identity.Backend),
                .Adapter = std::move(identity.Adapter),
                .DriverName = std::move(identity.DriverName),
                .DriverVersion = std::move(identity.DriverVersion),
                .DriverInformation = std::move(identity.DriverInformation),
                .DriverDetail = std::move(detail),
                .Frame = ActiveFrame ? ActiveFrame->Id : CaptureFrameId,
                .DeviceGeneration = DeviceGeneration.load(std::memory_order_acquire),
                .RecoveryAttempt = RecoveryAttemptsUsed.load(std::memory_order_acquire)};
    }
} // namespace Keire::RenderBackend
