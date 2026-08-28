#pragma once

#include "KeireInternal/Rendering/RenderFramePacketInternal.h"
#include "KeireInternal/Rendering/RenderStatisticsInternal.h"

#include <SDL3/SDL_gpu.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Keire::RenderBackend
{
    enum class DeviceRecoveryResult : std::uint8_t
    {
        Recovered,
        Failed,
        CancelledByShutdown
    };

    [[nodiscard]] inline bool TryBeginDeviceRecovery(std::atomic<RenderDeviceState>& lifecycle) noexcept
    {
        auto expected = RenderDeviceState::Running;
        return lifecycle.compare_exchange_strong(expected, RenderDeviceState::RecoveryPending,
                                                 std::memory_order_acq_rel);
    }

    inline void PublishTerminalDeviceFailure(std::atomic<RenderDeviceState>& lifecycle) noexcept
    {
        auto state = lifecycle.load(std::memory_order_acquire);
        while (state != RenderDeviceState::Closing && state != RenderDeviceState::Closed &&
               state != RenderDeviceState::Failed &&
               !lifecycle.compare_exchange_weak(state, RenderDeviceState::Failed, std::memory_order_acq_rel))
        {
        }
    }

    struct RenderQueueItem final
    {
        std::function<void()> Work;
        std::uint64_t Frame = 0;
        std::shared_ptr<RenderFramePacket> Packet;
    };

    enum class GpuVfxPipelineWarmupState : std::uint8_t
    {
        NotStarted,
        Compiling,
        Ready,
        Failed
    };

    struct RenderPipelineState
    {
        std::thread::id RenderThreadId;
        RenderStatistics CaptureStatistics;
        RenderStatistics PublishedStatistics;
        std::deque<RenderFrameTimeline> PublishedTimelines;
        mutable std::mutex PublicationMutex;
        std::uint64_t NextFrameId = 1;
        std::uint64_t CaptureFrameId = 0;
        std::chrono::steady_clock::time_point CaptureFrameStartedAt{};
        std::mutex RenderQueueMutex;
        std::condition_variable RenderQueueReady;
        std::condition_variable RenderQueueSpace;
        std::condition_variable FramesRetired;
        std::deque<RenderQueueItem> RenderQueue;
        std::deque<std::uint32_t> AvailableFrameSlots;
        CpuPreparationTracker CpuPreparation;
        std::jthread RenderThread;
        bool StopRenderQueue = false;
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        std::atomic<bool> InjectDeviceLossAtNextFrame{false};
        std::atomic<std::uint32_t> InjectDeviceLossAtRetirementMinimumInFlight{0};
        std::atomic<bool> InjectDeviceLossWithActiveResourcesAtNextFrame{false};
        std::atomic<bool> InjectCaptureFailureAtNextFrame{false};
        std::atomic<bool> InjectRecoveryAtAdmissionBarrier{false};
        std::atomic<bool> InjectPostSubmitFailureAtNextFrame{false};
        std::atomic<bool> InjectTerminalFailureAtNextAcceptedFrame{false};
        std::atomic<std::uint32_t> DelayNextAcceptedFrameMilliseconds{0};
        bool ThreadedHeadlessForTest = false;
        bool BlockNextAcceptedFrame = false;
        bool AcceptedFrameBlocked = false;
        bool ReleaseAcceptedFrame = false;
        std::uint32_t FrameAdmissionWaiters = 0;
        std::uint32_t RenderDispatchAdmissionWaiters = 0;
        std::atomic<std::uint64_t> SceneCaptureEnumerationCount{0};
        std::atomic<std::uint64_t> RuntimeUiCaptureEnumerationCount{0};
        std::atomic<std::uint64_t> LastCapturedDirectionalLightEntity{0};
        AssetId LastCapturedPrimaryScene;
        AssetId LastCapturedPrimaryBakedLighting;
        RenderCamera LastCapturedCamera;
        RenderEnvironmentSettings LastCapturedEnvironment;
        Color LastCapturedClearColor;
        std::vector<std::uint32_t> LastCapturedDrawContributionOrder;
        std::vector<EntityId> LastCapturedDrawEntities;
        std::vector<AssetId> LastCapturedSpatialScenes;
        std::vector<AssetId> LastCapturedSpatialBakedLighting;
        std::vector<std::uint32_t> LastPreparedOpaqueContributionOrder;
        std::vector<EntityId> LastPreparedOpaqueEntities;
        std::vector<std::uint32_t> LastPreparedTransparentContributionOrder;
        std::vector<EntityId> LastPreparedTransparentEntities;
        std::size_t LastCapturedLocalLights = 0;
        std::size_t LastCapturedReflectionProbes = 0;
        std::size_t LastCapturedLightProbeVolumes = 0;
        std::atomic<std::uint64_t> LostGenerationAbandonedHandleCount{0};
        std::atomic<std::uint64_t> LostGenerationGpuCleanupCallCount{0};
        std::atomic<std::uint64_t> HealthyRecoveryCandidateCleanupCount{0};
        std::atomic<std::uint64_t> LastRetriedVfxSnapshotCount{0};
        std::atomic<std::uint64_t> LastCapturedVfxSnapshotSignature{0};
        std::atomic<std::uint64_t> LastRetriedVfxSnapshotSignature{0};
        std::atomic<std::uint64_t> RenderedEditorUiFrameCount{0};
        std::atomic<std::uint32_t> InjectHealthyRecoveryCandidateFailures{0};
        std::atomic<std::uint32_t> InjectLostRecoveryCandidateFailures{0};
        std::atomic<float> LastRecoveryBackoffMillisecondsForTest{0.0F};
        bool InjectedDeviceLossForTest = false;
        std::atomic<std::uint64_t> ReleasedInjectedLostDeviceCountForTest{0};
#endif
        std::atomic<RenderDeviceState> DeviceLifecycle{RenderDeviceState::Running};
        std::atomic<std::uint32_t> OutstandingFrames{0};
        std::atomic<std::uint32_t> OutstandingHighWaterMark{0};
        std::atomic<std::uint64_t> AcceptedFrameCount{0};
        std::atomic<std::uint64_t> PresentedFrameCount{0};
        std::atomic<std::uint64_t> RetiredFrameCount{0};
        std::atomic<std::uint64_t> CancelledFrameCount{0};
        std::atomic<std::uint64_t> LastAcceptedFrameId{0};
        std::atomic<std::uint64_t> LastPresentedFrameId{0};
        std::atomic<std::uint64_t> LastRetiredFrameId{0};
        std::atomic<std::uint32_t> RenderQueueHighWaterMark{0};
        std::chrono::steady_clock::time_point LastFenceHealthProbeAt{};
        mutable std::mutex FailureMutex;
        std::exception_ptr TerminalFailure;
        std::optional<GpuDeviceLossDiagnostic> LastDeviceLossDiagnostic;
        std::string DeviceDriver;
        mutable std::mutex DeviceIdentityMutex;
        RenderDeviceIdentity DeviceIdentitySnapshot;
        std::atomic<std::uint32_t> DeviceGeneration{1};
        std::atomic<std::uint32_t> RecoveryAttemptsUsed{0};
        std::uint64_t RetiredFramesSinceRecovery = 0;
        std::chrono::steady_clock::time_point RecoveryStartedAt{};
        std::chrono::steady_clock::time_point LastRecoveryCompletedAt{};
        std::function<void()> BeforeDeviceRecovery;
        std::function<void(SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)> AfterDeviceRecovery;
        mutable std::mutex DeviceCallbackMutex;
        bool RecoveryOwnerBoundary = false;
        bool WindowRecreationRequested = false;
        bool WindowRecreationCompleted = false;
        std::exception_ptr WindowRecreationFailure;
        bool DeviceLost = false;
        bool FrameActive = false;
        bool FrameExecutionActive = false;
        bool Open = true;
    };
} // namespace Keire::RenderBackend
