#include "Keire/Rendering/RenderSystem.h"

#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Ui/RuntimeUi.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderSystemFacadeInternal.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Keire
{
    using RenderBackend::LastSdlError;

    SDL_GPUDevice* RenderSystemInternalAccess::Device(RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->Device;
    }

    SDL_Window* RenderSystemInternalAccess::NativeWindow(RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->NativeWindow;
    }

    SDL_GPUPresentMode RenderSystemInternalAccess::PresentMode(RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->PresentMode;
    }

    SDL_GPUTexture* RenderSystemInternalAccess::CaptureUiSurfaceTexture(const RenderSurface& surface)
    {
        const auto owner = surface.m_Impl->State->Owner.lock();
        return owner ? owner->CaptureUiSurfaceTexture(surface.m_Impl->State) : nullptr;
    }

    std::vector<std::uint8_t> RenderSystemInternalAccess::ReadbackRGBA8(RenderSystem& renderer,
                                                                        const RenderSurface& surface)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("ReadbackRGBA8");
        const auto token = renderState.CaptureSurfaceToken(surface.m_Impl->State);
        std::vector<std::uint8_t> pixels;
        renderState.DispatchRender(
            [&]
            {
                renderState.RequireRenderThread("ReadbackRGBA8");
                const auto resolved = renderState.ResolveSurface(token);
                if (!resolved || !resolved->Resources.PublishedColor() || resolved->Width == 0 || resolved->Height == 0)
                {
                    throw std::logic_error("Render surface is not available for readback.");
                }

                const std::uint64_t byteSize64 =
                    static_cast<std::uint64_t>(resolved->Width) * static_cast<std::uint64_t>(resolved->Height) * 4ULL;
                if (byteSize64 > std::numeric_limits<std::uint32_t>::max())
                    throw std::overflow_error("Render surface is too large for an RGBA8 readback.");
                const auto byteSize = static_cast<std::uint32_t>(byteSize64);

                SDL_GPUTransferBufferCreateInfo transferInformation{};
                transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                transferInformation.size = byteSize;
                SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(renderState.Device, &transferInformation);
                if (!transfer)
                    throw std::runtime_error("SDL_CreateGPUTransferBuffer(readback) failed: " + LastSdlError());

                SDL_GPUFence* fence = nullptr;
                try
                {
                    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(renderState.Device);
                    if (!commands)
                        throw std::runtime_error("SDL_AcquireGPUCommandBuffer(readback) failed: " + LastSdlError());
                    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
                    if (!copy)
                    {
                        (void)SDL_CancelGPUCommandBuffer(commands);
                        throw std::runtime_error("SDL_BeginGPUCopyPass(readback) failed: " + LastSdlError());
                    }

                    const SDL_GPUTextureRegion source{
                        resolved->Resources.PublishedColor(), 0, 0, 0, 0, 0, resolved->Width, resolved->Height, 1};
                    const SDL_GPUTextureTransferInfo destination{transfer, 0, resolved->Width, resolved->Height};
                    SDL_DownloadFromGPUTexture(copy, &source, &destination);
                    SDL_EndGPUCopyPass(copy);
                    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
                    if (!fence)
                        throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence(readback) failed: " +
                                                 LastSdlError());
                    if (!SDL_WaitForGPUFences(renderState.Device, true, &fence, 1))
                        throw std::runtime_error("SDL_WaitForGPUFences(readback) failed: " + LastSdlError());

                    const void* mapped = SDL_MapGPUTransferBuffer(renderState.Device, transfer, false);
                    if (!mapped)
                        throw std::runtime_error("SDL_MapGPUTransferBuffer(readback) failed: " + LastSdlError());
                    pixels.resize(byteSize);
                    std::memcpy(pixels.data(), mapped, byteSize);
                    SDL_UnmapGPUTransferBuffer(renderState.Device, transfer);
                    SDL_ReleaseGPUFence(renderState.Device, fence);
                    SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
                }
                catch (...)
                {
                    renderState.RethrowIfDeviceLost("render-surface readback");
                    if (fence)
                        SDL_ReleaseGPUFence(renderState.Device, fence);
                    SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
                    throw;
                }
            });
        return pixels;
    }

    std::vector<float> RenderSystemInternalAccess::ReadbackDirectionalShadow(RenderSystem& renderer,
                                                                             const RenderSurface& surface,
                                                                             const std::uint32_t layer)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("ReadbackDirectionalShadow");
        const auto& surfaceState = *surface.m_Impl->State;
        const auto owner = surfaceState.Owner.lock();
        if (owner.get() != &renderState)
            throw std::invalid_argument("Render surface belongs to another renderer.");
        if (renderState.ShadowDepthFormat != SDL_GPU_TEXTUREFORMAT_D32_FLOAT ||
            !surfaceState.PublishedWorkset().DirectionalShadow ||
            layer >= surfaceState.PublishedWorkset().DirectionalShadowLayers)
            throw std::logic_error("Directional shadow surface is not available for D32 readback.");

        const auto resolution = surfaceState.PublishedWorkset().DirectionalShadowResolution;
        const std::uint64_t byteSize64 =
            static_cast<std::uint64_t>(resolution) * static_cast<std::uint64_t>(resolution) * sizeof(float);
        if (byteSize64 > std::numeric_limits<std::uint32_t>::max())
            throw std::overflow_error("Directional shadow surface is too large for readback.");
        const auto byteSize = static_cast<std::uint32_t>(byteSize64);

        SDL_GPUTransferBufferCreateInfo transferInformation{};
        transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInformation.size = byteSize;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(renderState.Device, &transferInformation);
        if (!transfer)
            throw std::runtime_error("SDL_CreateGPUTransferBuffer(shadow readback) failed: " + LastSdlError());

        SDL_GPUFence* fence = nullptr;
        try
        {
            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(renderState.Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(shadow readback) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_BeginGPUCopyPass(shadow readback) failed: " + LastSdlError());
            }
            const SDL_GPUTextureRegion source{
                surfaceState.PublishedWorkset().DirectionalShadow, 0, layer, 0, 0, 0, resolution, resolution, 1};
            const SDL_GPUTextureTransferInfo destination{transfer, 0, resolution, resolution};
            SDL_DownloadFromGPUTexture(copy, &source, &destination);
            SDL_EndGPUCopyPass(copy);
            fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            if (!fence)
                throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence(shadow readback) failed: " +
                                         LastSdlError());
            if (!SDL_WaitForGPUFences(renderState.Device, true, &fence, 1))
                throw std::runtime_error("SDL_WaitForGPUFences(shadow readback) failed: " + LastSdlError());

            const void* mapped = SDL_MapGPUTransferBuffer(renderState.Device, transfer, false);
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(shadow readback) failed: " + LastSdlError());
            std::vector<float> depth(static_cast<std::size_t>(resolution) * resolution);
            std::memcpy(depth.data(), mapped, byteSize);
            SDL_UnmapGPUTransferBuffer(renderState.Device, transfer);
            SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            return depth;
        }
        catch (...)
        {
            renderState.RethrowIfDeviceLost("directional-shadow readback");
            if (fence)
                SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            throw;
        }
    }

    std::vector<float> RenderSystemInternalAccess::ReadbackLocalShadow(RenderSystem& renderer,
                                                                       const RenderSurface& surface,
                                                                       const std::uint32_t layer)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("ReadbackLocalShadow");
        const auto& surfaceState = *surface.m_Impl->State;
        const auto owner = surfaceState.Owner.lock();
        if (owner.get() != &renderState)
            throw std::invalid_argument("Render surface belongs to another renderer.");
        if (renderState.ShadowDepthFormat != SDL_GPU_TEXTUREFORMAT_D32_FLOAT ||
            !surfaceState.PublishedWorkset().LocalShadow || layer >= surfaceState.PublishedWorkset().LocalShadowLayers)
            throw std::logic_error("Local shadow surface is not available for D32 readback.");

        const auto resolution = surfaceState.PublishedWorkset().LocalShadowResolution;
        const std::uint64_t byteSize64 =
            static_cast<std::uint64_t>(resolution) * static_cast<std::uint64_t>(resolution) * sizeof(float);
        if (byteSize64 > std::numeric_limits<std::uint32_t>::max())
            throw std::overflow_error("Local shadow surface is too large for readback.");
        const auto byteSize = static_cast<std::uint32_t>(byteSize64);

        SDL_GPUTransferBufferCreateInfo transferInformation{};
        transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInformation.size = byteSize;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(renderState.Device, &transferInformation);
        if (!transfer)
            throw std::runtime_error("SDL_CreateGPUTransferBuffer(local shadow readback) failed: " + LastSdlError());

        SDL_GPUFence* fence = nullptr;
        try
        {
            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(renderState.Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(local shadow readback) failed: " +
                                         LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_BeginGPUCopyPass(local shadow readback) failed: " + LastSdlError());
            }
            const SDL_GPUTextureRegion source{
                surfaceState.PublishedWorkset().LocalShadow, 0, layer, 0, 0, 0, resolution, resolution, 1};
            const SDL_GPUTextureTransferInfo destination{transfer, 0, resolution, resolution};
            SDL_DownloadFromGPUTexture(copy, &source, &destination);
            SDL_EndGPUCopyPass(copy);
            fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            if (!fence)
                throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence(local shadow readback) failed: " +
                                         LastSdlError());
            if (!SDL_WaitForGPUFences(renderState.Device, true, &fence, 1))
                throw std::runtime_error("SDL_WaitForGPUFences(local shadow readback) failed: " + LastSdlError());

            const void* mapped = SDL_MapGPUTransferBuffer(renderState.Device, transfer, false);
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(local shadow readback) failed: " + LastSdlError());
            std::vector<float> depth(static_cast<std::size_t>(resolution) * resolution);
            std::memcpy(depth.data(), mapped, byteSize);
            SDL_UnmapGPUTransferBuffer(renderState.Device, transfer);
            SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            return depth;
        }
        catch (...)
        {
            renderState.RethrowIfDeviceLost("local-shadow readback");
            if (fence)
                SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            throw;
        }
    }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    void RenderSystemInternalAccess::InjectDeviceLoss(RenderSystem& renderer)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("InjectDeviceLoss");
        if (renderState.Specification.Mode != RenderMode::Rendered)
            throw std::logic_error("Device-loss injection requires a rendered backend.");
        renderState.InjectDeviceLossAtNextFrame.store(true, std::memory_order_release);
    }

    void RenderSystemInternalAccess::InjectDeviceLossAtRetirement(RenderSystem& renderer,
                                                                  const std::uint32_t minimumInFlight)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("InjectDeviceLossAtRetirement");
        if (renderState.Specification.Mode != RenderMode::Rendered)
            throw std::logic_error("Retirement device-loss injection requires a rendered backend.");
        if (minimumInFlight == 0U || minimumInFlight > renderState.Specification.MaximumFramesInFlight)
            throw std::invalid_argument("Retirement device-loss injection requires a reachable in-flight depth.");
        renderState.LostGenerationAbandonedHandleCount.store(0U, std::memory_order_release);
        renderState.LostGenerationGpuCleanupCallCount.store(0U, std::memory_order_release);
        renderState.InjectDeviceLossAtRetirementMinimumInFlight.store(minimumInFlight, std::memory_order_release);
    }

    void RenderSystemInternalAccess::InjectDeviceLossWithActiveResources(RenderSystem& renderer)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("InjectDeviceLossWithActiveResources");
        if (renderState.Specification.Mode != RenderMode::Rendered)
            throw std::logic_error("Active-resource device-loss injection requires a rendered backend.");
        renderState.LostGenerationAbandonedHandleCount.store(0U, std::memory_order_release);
        renderState.LostGenerationGpuCleanupCallCount.store(0U, std::memory_order_release);
        renderState.InjectDeviceLossWithActiveResourcesAtNextFrame.store(true, std::memory_order_release);
    }

    void RenderSystemInternalAccess::InjectCaptureFailure(RenderSystem& renderer) noexcept
    {
        renderer.m_Impl->State->InjectCaptureFailureAtNextFrame.store(true, std::memory_order_release);
    }

    void RenderSystemInternalAccess::InjectRecoveryAtAdmissionBarrier(RenderSystem& renderer) noexcept
    {
        renderer.m_Impl->State->InjectRecoveryAtAdmissionBarrier.store(true, std::memory_order_release);
    }

    void RenderSystemInternalAccess::InjectPostSubmitFailure(RenderSystem& renderer) noexcept
    {
        renderer.m_Impl->State->InjectPostSubmitFailureAtNextFrame.store(true, std::memory_order_release);
    }

    void RenderSystemInternalAccess::InjectRecoveryCandidateFailure(RenderSystem& renderer,
                                                                    const RenderRecoveryCandidateFault fault,
                                                                    const std::uint32_t count)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("InjectRecoveryCandidateFailure");
        if (state.Specification.Mode != RenderMode::Rendered)
            throw std::logic_error("Recovery-candidate failure injection requires a rendered backend.");
        if (count == 0U || count > 3U)
            throw std::invalid_argument("Recovery-candidate failure injection count must be in the range 1..3.");
        state.HealthyRecoveryCandidateCleanupCount.store(0U, std::memory_order_release);
        state.InjectHealthyRecoveryCandidateFailures.store(
            fault == RenderRecoveryCandidateFault::HealthyFailure ? count : 0U, std::memory_order_release);
        state.InjectLostRecoveryCandidateFailures.store(fault == RenderRecoveryCandidateFault::DeviceLoss ? count : 0U,
                                                        std::memory_order_release);
    }

    std::uint32_t RenderSystemInternalAccess::SaturateRendererQueue(RenderSystem& renderer)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("SaturateRendererQueue");
        if (!renderState.RenderThread.joinable())
            throw std::logic_error("Queue saturation requires an active renderer thread.");
        const auto capacity = static_cast<std::size_t>(renderState.Specification.MaximumFramesInFlight);
        if (capacity == 0U)
            throw std::logic_error("Queue saturation requires a non-zero configured capacity.");

        std::promise<void> workerStarted;
        auto workerStartedFuture = workerStarted.get_future();
        std::promise<void> releaseWorker;
        auto release = releaseWorker.get_future().share();
        std::exception_ptr producerFailure;
        std::mutex failureMutex;
        const auto produce = [&](const std::function<void()>& work)
        {
            try
            {
                renderState.DispatchRender(std::move(work));
            }
            catch (...)
            {
                std::scoped_lock lock(failureMutex);
                if (!producerFailure)
                    producerFailure = std::current_exception();
            }
        };

        std::jthread first;
        std::vector<std::jthread> producers;
        producers.reserve(capacity + 1U);
        bool workerReleased = false;
        const auto releaseWorkerOnce = [&]() noexcept
        {
            if (std::exchange(workerReleased, true))
                return;
            try
            {
                releaseWorker.set_value();
            }
            catch (...)
            {
            }
        };
        const auto joinWorkers = [&]
        {
            if (first.joinable())
                first.join();
            for (auto& producer : producers)
                if (producer.joinable())
                    producer.join();
        };

        std::uint32_t highWaterMark = 0;
        try
        {
            first = std::jthread(
                [&]
                {
                    produce(
                        [&]
                        {
                            workerStarted.set_value();
                            release.wait();
                        });
                });
            if (workerStartedFuture.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
                throw std::runtime_error("The renderer worker did not reach the queue-saturation barrier.");

            for (std::size_t index = 0; index < capacity; ++index)
                producers.emplace_back([&] { produce([] {}); });
            {
                std::unique_lock lock(renderState.RenderQueueMutex);
                const bool saturated = renderState.RenderQueueReady.wait_for(
                    lock, std::chrono::seconds(2), [&] { return renderState.RenderQueue.size() == capacity; });
                if (!saturated)
                    throw std::runtime_error("Renderer queue did not reach its configured capacity.");
            }

            producers.emplace_back([&] { produce([] {}); });
            {
                std::unique_lock lock(renderState.RenderQueueMutex);
                const bool overflowBlocked = renderState.RenderQueueReady.wait_for(
                    lock, std::chrono::seconds(2), [&] { return renderState.RenderDispatchAdmissionWaiters != 0U; });
                if (!overflowBlocked)
                    throw std::runtime_error("Renderer queue overflow producer did not block at admission.");
                highWaterMark = renderState.RenderQueueHighWaterMark.load(std::memory_order_relaxed);
            }
        }
        catch (...)
        {
            const auto failure = std::current_exception();
            releaseWorkerOnce();
            joinWorkers();
            if (producerFailure)
                std::rethrow_exception(producerFailure);
            std::rethrow_exception(failure);
        }
        releaseWorkerOnce();
        joinWorkers();
        if (producerFailure)
            std::rethrow_exception(producerFailure);
        return highWaterMark;
    }

    void RenderSystemInternalAccess::DelayNextAcceptedFrame(RenderSystem& renderer,
                                                            const std::uint32_t milliseconds) noexcept
    {
        renderer.m_Impl->State->DelayNextAcceptedFrameMilliseconds.store(milliseconds, std::memory_order_release);
    }

    void RenderSystemInternalAccess::InjectTerminalFailureAtNextAcceptedFrame(RenderSystem& renderer) noexcept
    {
        renderer.m_Impl->State->InjectTerminalFailureAtNextAcceptedFrame.store(true, std::memory_order_release);
    }

    bool RenderSystemInternalAccess::StartThreadedHeadlessForTest(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("StartThreadedHeadlessForTest");
        if (state.Specification.Mode != RenderMode::Headless)
            throw std::logic_error("The threaded headless renderer harness requires headless mode.");
        state.ThreadedHeadlessForTest = true;
        state.StartRenderThread();
        bool distinctThread = false;
        state.DispatchRender([&state, &distinctThread]
                             { distinctThread = std::this_thread::get_id() != state.OwnerThread; });
        return distinctThread;
    }

    void RenderSystemInternalAccess::BlockNextAcceptedFrame(RenderSystem& renderer) noexcept
    {
        std::scoped_lock lock(renderer.m_Impl->State->RenderQueueMutex);
        renderer.m_Impl->State->BlockNextAcceptedFrame = true;
        renderer.m_Impl->State->ReleaseAcceptedFrame = false;
    }

    bool RenderSystemInternalAccess::WaitForAcceptedFrameBlock(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        std::unique_lock lock(state.RenderQueueMutex);
        return state.FramesRetired.wait_for(lock, std::chrono::seconds(2),
                                            [&state]
                                            {
                                                return state.AcceptedFrameBlocked ||
                                                       state.DeviceLifecycle.load(std::memory_order_acquire) !=
                                                           RenderDeviceState::Running;
                                            }) &&
               state.AcceptedFrameBlocked;
    }

    bool RenderSystemInternalAccess::WaitForFrameAdmissionWaiter(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        std::unique_lock lock(state.RenderQueueMutex);
        return state.FramesRetired.wait_for(lock, std::chrono::seconds(2),
                                            [&state]
                                            {
                                                return state.FrameAdmissionWaiters != 0U ||
                                                       state.DeviceLifecycle.load(std::memory_order_acquire) !=
                                                           RenderDeviceState::Running;
                                            }) &&
               state.FrameAdmissionWaiters != 0U;
    }

    void RenderSystemInternalAccess::ReleaseAcceptedFrameBlock(RenderSystem& renderer) noexcept
    {
        {
            std::scoped_lock lock(renderer.m_Impl->State->RenderQueueMutex);
            renderer.m_Impl->State->ReleaseAcceptedFrame = true;
        }
        renderer.m_Impl->State->FramesRetired.notify_all();
    }

    std::uint64_t RenderSystemInternalAccess::SceneCaptureEnumerationCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->SceneCaptureEnumerationCount.load(std::memory_order_relaxed);
    }

    std::uint64_t RenderSystemInternalAccess::RuntimeUiCaptureEnumerationCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->RuntimeUiCaptureEnumerationCount.load(std::memory_order_relaxed);
    }

    std::uint64_t RenderSystemInternalAccess::LastCapturedDirectionalLightEntity(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->LastCapturedDirectionalLightEntity.load(std::memory_order_relaxed);
    }

    AdditiveSceneCaptureSummary RenderSystemInternalAccess::LastCapturedAdditiveScene(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("LastCapturedAdditiveScene");
        std::scoped_lock lock(state.PublicationMutex);
        return {.PrimaryScene = state.LastCapturedPrimaryScene,
                .PrimaryBakedLighting = state.LastCapturedPrimaryBakedLighting,
                .Camera = state.LastCapturedCamera,
                .Environment = state.LastCapturedEnvironment,
                .ClearColor = state.LastCapturedClearColor,
                .DrawContributionOrder = state.LastCapturedDrawContributionOrder,
                .DrawEntities = state.LastCapturedDrawEntities,
                .SpatialScenes = state.LastCapturedSpatialScenes,
                .SpatialBakedLighting = state.LastCapturedSpatialBakedLighting,
                .PreparedOpaqueContributionOrder = state.LastPreparedOpaqueContributionOrder,
                .PreparedOpaqueEntities = state.LastPreparedOpaqueEntities,
                .PreparedTransparentContributionOrder = state.LastPreparedTransparentContributionOrder,
                .PreparedTransparentEntities = state.LastPreparedTransparentEntities,
                .LocalLights = state.LastCapturedLocalLights,
                .ReflectionProbes = state.LastCapturedReflectionProbes,
                .LightProbeVolumes = state.LastCapturedLightProbeVolumes};
    }

    std::size_t RenderSystemInternalAccess::AvailableFrameSlotCount(const RenderSystem& renderer) noexcept
    {
        std::scoped_lock lock(renderer.m_Impl->State->RenderQueueMutex);
        return renderer.m_Impl->State->AvailableFrameSlots.size();
    }

    std::uint64_t RenderSystemInternalAccess::LostGenerationAbandonedHandleCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->LostGenerationAbandonedHandleCount.load(std::memory_order_acquire);
    }

    std::uint64_t RenderSystemInternalAccess::LostGenerationGpuCleanupCallCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->LostGenerationGpuCleanupCallCount.load(std::memory_order_acquire);
    }

    std::uint64_t
    RenderSystemInternalAccess::HealthyRecoveryCandidateCleanupCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->HealthyRecoveryCandidateCleanupCount.load(std::memory_order_acquire);
    }

    std::uint64_t
    RenderSystemInternalAccess::ReleasedInjectedLostDeviceCountForTest(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->ReleasedInjectedLostDeviceCountForTest.load(std::memory_order_acquire);
    }

    std::uint64_t RenderSystemInternalAccess::LastRetriedVfxSnapshotCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->LastRetriedVfxSnapshotCount.load(std::memory_order_acquire);
    }

    std::array<std::uint64_t, 2>
    RenderSystemInternalAccess::LastVfxRetrySignaturesForTest(const RenderSystem& renderer) noexcept
    {
        return {renderer.m_Impl->State->LastCapturedVfxSnapshotSignature.load(std::memory_order_acquire),
                renderer.m_Impl->State->LastRetriedVfxSnapshotSignature.load(std::memory_order_acquire)};
    }

    RenderRecoveryResourceCounts RenderSystemInternalAccess::RecoveryResourceCountsForTest(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("RecoveryResourceCountsForTest");
        RenderRecoveryResourceCounts result;
        state.DispatchRender(
            [&state, &result]
            {
                result.Meshes = state.MeshCache.size();
                result.Textures = state.TextureCache.size();
                result.Materials = state.MaterialCache.size();
                result.Shaders = state.ShaderCache.size();
                result.GpuVfxWorlds = state.GpuVfxWorlds.size();
                result.RenderedEditorUiFrames = state.RenderedEditorUiFrameCount.load(std::memory_order_acquire);
            });
        return result;
    }

    std::uint64_t RenderSystemInternalAccess::SurfaceResourceGenerationForTest(const RenderSurface& surface)
    {
        const auto surfaceState = surface.m_Impl->State;
        const auto owner = surfaceState->Owner.lock();
        if (!owner)
            return 0U;
        owner->RequireOwner("SurfaceResourceGenerationForTest");
        std::uint64_t result = 0U;
        owner->DispatchRender([&result, surfaceState] { result = surfaceState->Generation; });
        return result;
    }

    std::uint32_t RenderSystemInternalAccess::RecoveryAttemptCountForTest(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        if (std::this_thread::get_id() != state.OwnerThread)
            throw std::logic_error(
                "RenderSystem::RecoveryAttemptCountForTest must be called on the application owner thread.");
        return state.RecoveryAttemptsUsed.load(std::memory_order_acquire);
    }

    float RenderSystemInternalAccess::LastRecoveryBackoffMillisecondsForTest(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("LastRecoveryBackoffMillisecondsForTest");
        return state.LastRecoveryBackoffMillisecondsForTest.load(std::memory_order_acquire);
    }

    void RenderSystemInternalAccess::SatisfyRecoveryStabilityWindowForTest(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("SatisfyRecoveryStabilityWindowForTest");
        state.DispatchRender(
            [&state]
            {
                state.LastRecoveryCompletedAt = std::chrono::steady_clock::now() - std::chrono::seconds(60);
                state.RetiredFramesSinceRecovery = 119U;
            });
    }

    bool RenderSystemInternalAccess::CompleteFrameTwiceForTest(RenderSystem& renderer)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("CompleteFrameTwiceForTest");
        auto frame = std::make_shared<RenderBackend::RenderFramePacket>();
        const auto timelineCount = [&state]
        {
            std::scoped_lock lock(state.PublicationMutex);
            return state.PublishedTimelines.size();
        }();
        const auto retiredCount = state.RetiredFrameCount.load(std::memory_order_acquire);
        const auto cancelledCount = state.CancelledFrameCount.load(std::memory_order_acquire);
        std::size_t availableBefore = 0;
        {
            std::scoped_lock lock(state.RenderQueueMutex);
            availableBefore = state.AvailableFrameSlots.size();
            if (availableBefore == 0U)
                return false;
            frame->FrameSlot = state.AvailableFrameSlots.front();
            state.AvailableFrameSlots.pop_front();
        }
        frame->Id = 0xFFFF'FFFFU;
        frame->SubmittedAt = std::chrono::steady_clock::now();
        frame->PresentedAt = frame->SubmittedAt;
        state.OutstandingFrames.fetch_add(1U, std::memory_order_acq_rel);
        state.CompleteFrame(frame, false);
        state.CompleteFrame(frame, true);
        std::size_t availableAfter = 0;
        {
            std::scoped_lock lock(state.RenderQueueMutex);
            availableAfter = state.AvailableFrameSlots.size();
        }
        const auto timelineCountAfter = [&state]
        {
            std::scoped_lock lock(state.PublicationMutex);
            return state.PublishedTimelines.size();
        }();
        return availableAfter == availableBefore && state.OutstandingFrames.load(std::memory_order_acquire) == 0U &&
               state.RetiredFrameCount.load(std::memory_order_acquire) == retiredCount + 1U &&
               state.CancelledFrameCount.load(std::memory_order_acquire) == cancelledCount &&
               timelineCountAfter == timelineCount + 1U;
    }

    std::optional<GpuDeviceLossDiagnostic>
    RenderSystemInternalAccess::ClassifyDeviceFailureForTest(const RenderSystem& renderer, std::string operation,
                                                             std::string detail)
    {
        return renderer.m_Impl->State->ClassifyDeviceFailure(std::move(operation), std::move(detail));
    }
#endif

    void RenderSystemInternalAccess::RequestSurfaceSize(RenderSurface& surface, const std::uint32_t width,
                                                        const std::uint32_t height)
    {
        if (width > 16384 || height > 16384)
            throw std::invalid_argument("Test surface dimensions must be in the range 0..16384.");
        if (const auto owner = surface.m_Impl->State->Owner.lock())
        {
            owner->RequireOwner("RequestSurfaceSize");
            if (surface.m_Impl->State->RequestedWidth != width || surface.m_Impl->State->RequestedHeight != height)
                surface.m_Impl->State = owner->CreateSurfaceEpoch(surface.m_Impl->State, width, height);
        }
    }

    void RenderSystemInternalAccess::SetPresentationSurface(RenderSystem& renderer, const Ref<RenderSurface>& surface)
    {
        auto& rendererState = renderer.m_Impl->State;
        rendererState->RequireOwner("SetPresentationSurface");
        if (!surface)
        {
            rendererState->PresentationSurfaceId.reset();
            return;
        }
        const auto surfaceOwner = surface->m_Impl->State->Owner.lock();
        if (surfaceOwner != rendererState)
            throw std::invalid_argument("A presentation surface must belong to its renderer.");
        rendererState->PresentationSurfaceId = surface->m_Impl->State->Id;
    }

    std::size_t RenderSystemInternalAccess::RuntimeUiCommandCount(const RenderSystem& renderer) noexcept
    {
        std::size_t count = renderer.m_Impl->State->CaptureRuntimeUiCommands.size();
        for (const auto& tree : renderer.m_Impl->State->PendingRuntimeUiTrees)
            if (tree)
                count += tree->DrawCommands().size();
        return count;
    }

    std::size_t RenderSystemInternalAccess::SceneContributionCount(const RenderSystem& renderer,
                                                                   const RenderSurface& surface) noexcept
    {
        const auto& requests = renderer.m_Impl->State->PendingSceneRequests;
        const auto found = std::ranges::find_if(requests,
                                                [&surface](const auto& request)
                                                {
                                                    return request.Surface.Id == surface.m_Impl->State->Id &&
                                                           request.Surface.Epoch == surface.m_Impl->State->Epoch;
                                                });
        return found == requests.end() || !found->Request.DrawSceneContributions
                   ? 0U
                   : found->Request.AdditionalScenes.size() + 1U;
    }

    std::size_t RenderSystemInternalAccess::SceneDrawItemCount(const RenderSystem& renderer,
                                                               const RenderSurface& surface) noexcept
    {
        const auto& requests = renderer.m_Impl->State->PendingSceneRequests;
        const auto found = std::ranges::find_if(requests,
                                                [&surface](const auto& request)
                                                {
                                                    return request.Surface.Id == surface.m_Impl->State->Id &&
                                                           request.Surface.Epoch == surface.m_Impl->State->Epoch;
                                                });
        if (found == requests.end() || !found->Request.DrawSceneContributions)
            return 0U;
        const auto countScene = [](const Ref<Scene>& scene)
        { return scene ? scene->Query<MeshRendererComponent>().size() : 0U; };
        std::size_t count = countScene(found->Request.Scene);
        for (const auto& contribution : found->Request.AdditionalScenes)
            count += countScene(contribution.Scene);
        return count;
    }

    void* RenderSystemInternalAccess::SurfaceState(RenderSurface& surface) noexcept
    {
        return surface.m_Impl->State.get();
    }

    std::shared_ptr<void> RenderSystemInternalAccess::SurfaceLease(const RenderSurface& surface) noexcept
    {
        return surface.m_Impl->State;
    }

    void RenderSystemInternalAccess::SetDeviceRecoveryCallbacks(
        RenderSystem& renderer, std::function<void()> before,
        std::function<void(SDL_GPUDevice*, SDL_GPUTextureFormat, SDL_GPUPresentMode)> after)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("SetDeviceRecoveryCallbacks");
        std::scoped_lock lock(state.DeviceCallbackMutex);
        state.BeforeDeviceRecovery = std::move(before);
        state.AfterDeviceRecovery = std::move(after);
    }

    void RenderSystemInternalAccess::SetUiContextAccess(RenderSystem& renderer,
                                                        std::shared_ptr<Detail::UiContextAccess> contextAccess)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("SetUiContextAccess");
        state.EditorUiContextAccess.store(std::move(contextAccess), std::memory_order_release);
    }

    void RenderSystemInternalAccess::RunOnRenderThread(RenderSystem& renderer, std::function<void()> work)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("RunOnRenderThread");
        state.DispatchRender(std::move(work));
    }

    bool RenderSystemInternalAccess::GpuLifecycleThreadAffinityValid(const RenderSystem& renderer) noexcept
    {
        const auto& state = *renderer.m_Impl->State;
        if (state.Specification.Mode != RenderMode::Rendered)
            return true;
        return state.GpuCreationThread != std::thread::id{} && state.GpuCreationThread != state.OwnerThread &&
               (state.GpuDestructionThread == std::thread::id{} ||
                state.GpuDestructionThread == state.GpuCreationThread);
    }

    bool RenderSystemInternalAccess::WaitForDeviceRecovery(RenderSystem& renderer,
                                                           std::function<bool()> pumpWindowEvents)
    {
        auto& state = *renderer.m_Impl->State;
        state.RequireOwner("WaitForDeviceRecovery");
        return state.WaitForRecoveryAtOwnerBoundary(pumpWindowEvents);
    }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
    void RenderSystemInternalAccess::SetDeviceRecoveryStateForTest(RenderSystem& renderer,
                                                                   const RenderDeviceState state) noexcept
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.DeviceLifecycle.store(state, std::memory_order_release);
        renderState.FramesRetired.notify_all();
    }
#endif

    void RenderSystemInternalAccess::WaitIdle(RenderSystem& renderer) noexcept
    {
        try
        {
            renderer.m_Impl->State->Flush();
        }
        catch (...)
        {
        }
    }

    std::exception_ptr RenderSystemInternalAccess::TerminalFailure(const RenderSystem& renderer) noexcept
    {
        std::scoped_lock lock(renderer.m_Impl->State->FailureMutex);
        return renderer.m_Impl->State->TerminalFailure;
    }

    std::uint64_t RenderSystemInternalAccess::MaterialBindingBuildCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->MaterialBindingBuilds;
    }

    std::uint64_t RenderSystemInternalAccess::MaterialDependencyCheckCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->MaterialDependencyChecks;
    }

    std::uint64_t RenderSystemInternalAccess::SkinningStaticBuildCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->SkinningStaticBuilds;
    }

    std::uint64_t RenderSystemInternalAccess::SkinningOutputBuildCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->SkinningOutputBuilds;
    }

    void RenderSystemInternalAccess::BeginFrame(RenderSystem& renderer) { renderer.m_Impl->State->BeginFrame(); }
    void RenderSystemInternalAccess::CancelFrame(RenderSystem& renderer) noexcept
    {
        renderer.m_Impl->State->CancelFrame();
    }
    void RenderSystemInternalAccess::EndFrame(RenderSystem& renderer, ImDrawData* drawData)
    {
        renderer.m_Impl->State->EndFrame(drawData);
    }
} // namespace Keire
