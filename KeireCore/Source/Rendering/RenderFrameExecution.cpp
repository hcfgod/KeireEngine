#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::RenderBackend
{
    void RenderSharedState::EndFrame(ImDrawData* drawData)
    {
        RequireOwner("EndFrame");
        if (!FrameActive)
            throw std::logic_error("No render frame is active.");
        FrameActive = false;

        if (Specification.Mode == RenderMode::Headless)
        {
            Statistics.Surfaces = static_cast<std::uint32_t>(LiveSurfaces().size());
            Statistics.Passes = Statistics.Surfaces;
            return;
        }

        const auto started = std::chrono::steady_clock::now();
        DispatchRender([this, drawData] { ExecuteFrame(drawData); });
        Statistics.RendererLatencyMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    void RenderSharedState::ExecuteFrame(ImDrawData* drawData)
    {
        Statistics.CommandRecordingMilliseconds = 0.0F;
        Statistics.SkinningPreparationMilliseconds = 0.0F;
        Statistics.VfxPreparationMilliseconds = 0.0F;
        Statistics.DrawPreparationMilliseconds = 0.0F;
        Statistics.ShadowRecordingMilliseconds = 0.0F;
        Statistics.ForwardPlusCullingMilliseconds = 0.0F;
        Statistics.ScenePassMilliseconds = 0.0F;
        Statistics.DepthPassMilliseconds = 0.0F;
        Statistics.ToneMapMilliseconds = 0.0F;
        Statistics.CommandRecordingUnattributedMilliseconds = 0.0F;
        Statistics.FrameUploadMilliseconds = 0.0F;
        Statistics.SwapchainWaitMilliseconds = 0.0F;
        Statistics.UiRecordingMilliseconds = 0.0F;
        Statistics.GpuSubmissionMilliseconds = 0.0F;
        Statistics.GpuFrameMilliseconds = 0.0F;
        Statistics.ForwardPlusCacheHits = 0;
        Statistics.FrameUploadSubmissions = 0;
        if (InjectDeviceLossAtNextFrame.exchange(false, std::memory_order_acq_rel))
            throw std::runtime_error("Injected GPU device loss.");
        if (FrameUploadCommands || FrameUploadPass || !FrameUploadTransfers.empty())
            throw std::logic_error("A previous frame left the GPU upload context active.");

        if (GpuSubmissionSerial == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("GPU submission serial exhausted.");
        SDL_GPUCommandBuffer* commands = nullptr;
        std::vector<SDL_GPUCommandBuffer*> surfaceCommands;
        surfaceCommands.reserve(Requests.size());
        ActiveGpuSubmissionSerial = GpuSubmissionSerial + 1U;
        FrameExecutionActive = true;
        bool gpuWorkSubmitted = false;

        try
        {
            const auto recordingStarted = std::chrono::steady_clock::now();
            for (const auto& request : Requests)
            {
                auto* surfaceCommandBuffer = SDL_AcquireGPUCommandBuffer(Device);
                if (!surfaceCommandBuffer)
                    throw std::runtime_error("SDL_AcquireGPUCommandBuffer(surface) failed: " + LastSdlError());
                surfaceCommands.push_back(surfaceCommandBuffer);
                RecordSurface(surfaceCommandBuffer, *request.Surface, surfaceCommands);
            }
            Statistics.CommandRecordingMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - recordingStarted).count();
            const auto attributedRecordingMilliseconds =
                Statistics.SkinningPreparationMilliseconds + Statistics.VfxPreparationMilliseconds +
                Statistics.DrawPreparationMilliseconds + Statistics.ShadowRecordingMilliseconds +
                Statistics.ForwardPlusCullingMilliseconds + Statistics.ScenePassMilliseconds +
                Statistics.DepthPassMilliseconds + Statistics.ToneMapMilliseconds;
            Statistics.CommandRecordingUnattributedMilliseconds =
                std::max(0.0F, Statistics.CommandRecordingMilliseconds - attributedRecordingMilliseconds);

            const auto uploadStarted = std::chrono::steady_clock::now();
            if (FrameUploadPass)
            {
                SDL_EndGPUCopyPass(FrameUploadPass);
                FrameUploadPass = nullptr;
            }
            if (FrameUploadCommands)
            {
                auto* uploadCommands = std::exchange(FrameUploadCommands, nullptr);
                if (FrameUploadTransfers.empty())
                {
                    (void)SDL_CancelGPUCommandBuffer(uploadCommands);
                }
                else if (!SDL_SubmitGPUCommandBuffer(uploadCommands))
                {
                    throw std::runtime_error("SDL_SubmitGPUCommandBuffer(frame uploads) failed: " + LastSdlError());
                }
                else
                {
                    gpuWorkSubmitted = true;
                    ++Statistics.FrameUploadSubmissions;
                }
            }
            Statistics.FrameUploadMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - uploadStarted).count();

            const auto submissionStarted = std::chrono::steady_clock::now();
            // SDL's D3D12 backend owns bounded descriptor heaps per command buffer. Keeping independent editor
            // surfaces in one command buffer allows the Scene, Game, and preview views to exhaust a shared heap.
            // Submitting each offscreen surface separately preserves queue order while resetting that backend-local
            // allocation. The final swapchain fence still retires every earlier submission on the same GPU queue.
            for (auto& surfaceCommandBuffer : surfaceCommands)
            {
                auto* submitted = std::exchange(surfaceCommandBuffer, nullptr);
                if (!SDL_SubmitGPUCommandBuffer(submitted))
                    throw std::runtime_error("SDL_SubmitGPUCommandBuffer(surface) failed: " + LastSdlError());
                gpuWorkSubmitted = true;
            }

            commands = SDL_AcquireGPUCommandBuffer(Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(swapchain) failed: " + LastSdlError());
            RecordSwapchain(commands, drawData);

            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            commands = nullptr;
            if (!fence)
                throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence failed: " + LastSdlError());
            gpuWorkSubmitted = true;
            Statistics.GpuSubmissionMilliseconds =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - submissionStarted).count();
            InFlight.push_back({fence, std::move(PendingRetired), std::move(PendingRetiredMeshes),
                                std::move(PendingRetiredSkins), std::move(PendingRetiredTextures),
                                std::move(PendingRetiredPipelines), std::move(PendingRetiredForwardPlus),
                                std::move(FrameTransientBuffers), std::move(FrameUploadTransfers), submissionStarted,
                                Statistics.VfxGpuWorlds != 0, PendingRetiredBytes});
            PendingRetiredBytes = 0;
            PendingRetired.clear();
            PendingRetiredMeshes.clear();
            PendingRetiredSkins.clear();
            PendingRetiredTextures.clear();
            PendingRetiredPipelines.clear();
            PendingRetiredForwardPlus.clear();
            FrameTransientBuffers.clear();
            FrameUploadTransfers.clear();
            GpuSubmissionSerial = ActiveGpuSubmissionSerial;
            ActiveGpuSubmissionSerial = 0;
            FrameExecutionActive = false;
            for (const auto& request : Requests)
            {
                auto* surface = request.Surface;
                if (surface->HasOutput)
                    std::swap(surface->Resources.SampledColor, surface->Resources.ExchangeColor);
                else
                    surface->HasOutput = true;
            }
        }
        catch (...)
        {
            if (FrameUploadPass)
            {
                SDL_EndGPUCopyPass(FrameUploadPass);
                FrameUploadPass = nullptr;
            }
            if (FrameUploadCommands)
            {
                (void)SDL_CancelGPUCommandBuffer(FrameUploadCommands);
                FrameUploadCommands = nullptr;
            }
            for (auto*& surfaceCommandBuffer : surfaceCommands)
            {
                if (surfaceCommandBuffer)
                    (void)SDL_CancelGPUCommandBuffer(std::exchange(surfaceCommandBuffer, nullptr));
            }
            if (gpuWorkSubmitted && Device)
                (void)SDL_WaitForGPUIdle(Device);
            for (auto* transfer : FrameUploadTransfers)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            FrameUploadTransfers.clear();
            if (commands)
                (void)SDL_CancelGPUCommandBuffer(commands);
            for (auto* buffer : FrameTransientBuffers)
                SDL_ReleaseGPUBuffer(Device, buffer);
            FrameTransientBuffers.clear();
            // A canceled command buffer invalidates the emitter sequencing recorded for every world it touched.
            for (auto& [worldId, resources] : GpuVfxWorlds)
            {
                (void)worldId;
                if (resources.LastPreparedFrame != Statistics.Frame)
                    continue;
                resources.InvalidateSequencing();
            }
            FrameActive = false;
            ActiveGpuSubmissionSerial = 0;
            FrameExecutionActive = false;
            throw;
        }
    }

    void RenderSharedState::Close() noexcept
    {
        if (!Open)
            return;
        Open = false;
        FrameActive = false;
        if (VfxPipelineWarmupJob)
        {
            VfxPipelineWarmupJob.Cancel();
            (void)VfxPipelineWarmupJob.Wait();
            VfxPipelineWarmupJob = {};
        }
        if (RenderJobs)
        {
            RenderJobs->Cancel();
            RenderJobs->Wait();
        }
        StopRenderThread();
        FrameExecutionActive = false;
        ActiveGpuSubmissionSerial = 0;
        if (Device)
            (void)SDL_WaitForGPUIdle(Device);
        for (const auto& surface : LiveSurfaces())
        {
            ReleaseResources(surface->Resources);
            ReleaseForwardPlusResources(surface->ForwardPlus);
            surface->Owner.reset();
            surface->Width = 0;
            surface->Height = 0;
        }
        for (auto& resources : PendingRetired)
            ReleaseResources(resources);
        PendingRetired.clear();
        std::uint64_t pendingRetiredMeshBytes = 0;
        for (auto& resources : PendingRetiredMeshes)
        {
            pendingRetiredMeshBytes += resources.EstimatedBytes;
            ReleaseMeshResources(resources);
        }
        PendingRetiredMeshes.clear();
        for (auto& resources : PendingRetiredSkins)
            ReleaseGpuSkinResources(resources);
        PendingRetiredSkins.clear();
        std::uint64_t pendingRetiredTextureBytes = 0;
        for (auto& resources : PendingRetiredTextures)
        {
            pendingRetiredTextureBytes += resources.EstimatedBytes;
            ReleaseTextureResources(resources);
        }
        PendingRetiredTextures.clear();
        if (Streaming)
        {
            Streaming->ReleaseRetired(StreamingClass::Mesh, 0, pendingRetiredMeshBytes);
            Streaming->ReleaseRetired(StreamingClass::Texture, 0, pendingRetiredTextureBytes);
        }
        for (auto* pipeline : PendingRetiredPipelines)
            SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
        PendingRetiredPipelines.clear();
        for (auto& resources : PendingRetiredForwardPlus)
            ReleaseForwardPlusResources(resources);
        PendingRetiredForwardPlus.clear();
        PendingRetiredBytes = 0;
        Statistics.FenceRetiredBytes = 0;
        for (auto* buffer : FrameTransientBuffers)
            SDL_ReleaseGPUBuffer(Device, buffer);
        FrameTransientBuffers.clear();
        for (auto* transfer : FrameUploadTransfers)
            SDL_ReleaseGPUTransferBuffer(Device, transfer);
        FrameUploadTransfers.clear();
        FrameUploadPass = nullptr;
        FrameUploadCommands = nullptr;
        for (auto& frame : InFlight)
        {
            std::uint64_t retiredMeshBytes = 0;
            for (const auto& resources : frame.RetiredMeshes)
                retiredMeshBytes += resources.EstimatedBytes;
            std::uint64_t retiredTextureBytes = 0;
            for (const auto& resources : frame.RetiredTextures)
                retiredTextureBytes += resources.EstimatedBytes;
            for (auto& resources : frame.Retired)
                ReleaseResources(resources);
            for (auto& resources : frame.RetiredMeshes)
                ReleaseMeshResources(resources);
            for (auto& resources : frame.RetiredSkins)
                ReleaseGpuSkinResources(resources);
            for (auto& resources : frame.RetiredTextures)
                ReleaseTextureResources(resources);
            for (auto* pipeline : frame.RetiredPipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
            for (auto& resources : frame.RetiredForwardPlus)
                ReleaseForwardPlusResources(resources);
            for (auto* buffer : frame.TransientBuffers)
                SDL_ReleaseGPUBuffer(Device, buffer);
            for (auto* transfer : frame.TransientTransferBuffers)
                SDL_ReleaseGPUTransferBuffer(Device, transfer);
            if (Streaming)
            {
                Streaming->ReleaseRetired(StreamingClass::Mesh, 0, retiredMeshBytes);
                Streaming->ReleaseRetired(StreamingClass::Texture, 0, retiredTextureBytes);
            }
            if (Device && frame.Fence)
                SDL_ReleaseGPUFence(Device, frame.Fence);
        }
        InFlight.clear();
        Requests.clear();
        RuntimeUiCommands.clear();
        for (auto& pipelines : Pipelines)
        {
            if (pipelines.GpuVfxMesh)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.GpuVfxMesh);
            if (pipelines.GpuVfxRibbon)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.GpuVfxRibbon);
            if (pipelines.GpuVfx)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.GpuVfx);
            if (pipelines.Vfx)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Vfx);
            if (pipelines.Sky)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Sky);
            if (pipelines.Grid)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Grid);
            if (pipelines.Cube)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Cube);
        }
        Pipelines.clear();
        for (auto& [id, entry] : MeshCache)
        {
            (void)id;
            ReleaseMeshResources(entry.Resources);
        }
        MeshCache.clear();
        for (auto& [id, entry] : SkinCache)
        {
            (void)id;
            ReleaseGpuSkinResources(entry.Resources);
        }
        SkinCache.clear();
        for (auto& [id, entry] : TextureCache)
        {
            (void)id;
            ReleaseTextureResources(entry.Resources);
        }
        TextureCache.clear();
        for (auto& [id, entry] : LightingTextureCache)
        {
            (void)id;
            ReleaseTextureResources(entry.Resources);
        }
        LightingTextureCache.clear();
        LightingSetCache.clear();
        LightProbeVolumeCache.clear();
        MaterialCache.clear();
        VfxVolumeCache.clear();
        for (auto& [id, entry] : ShaderCache)
        {
            (void)id;
            for (const auto& pipeline : entry.Pipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline.Handle);
        }
        ShaderCache.clear();
        ReleaseTextureResources(CheckerboardTexture);
        ReleaseTextureResources(DefaultSkyTexture);
        ReleaseTextureResources(BrdfIntegrationLut);
        ReleaseTextureResources(WhiteTexture);
        ReleaseTextureResources(FlatNormalTexture);
        ReleaseTextureResources(NeutralOrmTexture);
        ReleaseTextureResources(BlackTexture);
        ReleaseTextureResources(BlackDataTexture);
        ReleaseTextureResources(WhiteDataTexture);
        ReleaseTextureResources(DefaultLightingArray);
        ReleaseTextureResources(DefaultLightingMaskArray);
        ReleaseTextureResources(DefaultReflectionCubeArray);
        ReleaseTextureResources(CookieAtlas);
        for (const auto& [description, sampler] : SamplerCache)
        {
            (void)description;
            SDL_ReleaseGPUSampler(Device, sampler);
        }
        SamplerCache.clear();
        if (ShadowSampler)
            SDL_ReleaseGPUSampler(Device, ShadowSampler);
        ShadowSampler = nullptr;
        if (ToneMapSampler)
            SDL_ReleaseGPUSampler(Device, ToneMapSampler);
        ToneMapSampler = nullptr;
        if (EmptyShadowTexture)
            SDL_ReleaseGPUTexture(Device, EmptyShadowTexture);
        EmptyShadowTexture = nullptr;
        if (ShadowPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, ShadowPipeline);
        ShadowPipeline = nullptr;
        if (SceneDepthPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, SceneDepthPipeline);
        SceneDepthPipeline = nullptr;
        if (SkinningPipeline)
            SDL_ReleaseGPUComputePipeline(Device, SkinningPipeline);
        SkinningPipeline = nullptr;
        SkinningPipelineAttempted = false;
        for (auto& [world, resources] : GpuVfxWorlds)
        {
            (void)world;
            ReleaseGpuVfxWorld(resources);
        }
        GpuVfxWorlds.clear();
        ReleaseGpuVfxPipelines();
        VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::NotStarted, std::memory_order_relaxed);
        if (ToneMapPipeline)
            SDL_ReleaseGPUGraphicsPipeline(Device, ToneMapPipeline);
        ToneMapPipeline = nullptr;
        if (const auto pipeline = std::exchange(RuntimeUiPipeline, nullptr))
            SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
        ReleaseMeshResources(ErrorMesh);
        ReleaseMeshResources(DefaultMesh);
        if (WindowClaimed && Device && NativeWindow)
            SDL_ReleaseWindowFromGPUDevice(Device, NativeWindow);
        WindowClaimed = false;
        if (Device)
            SDL_DestroyGPUDevice(Device);
        Device = nullptr;
        NativeWindow = nullptr;
        Window.Reset();
        Windows.Reset();
        Assets.Reset();
    }
} // namespace Keire::RenderBackend
