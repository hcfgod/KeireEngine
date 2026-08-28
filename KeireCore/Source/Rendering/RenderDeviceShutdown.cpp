#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <utility>

namespace Keire::RenderBackend
{
#if defined(KEIRE_ENABLE_TEST_HOOKS)
    void RenderSharedState::MarkInjectedDeviceLossForTest() noexcept { InjectedDeviceLossForTest = true; }

    bool
    RenderSharedState::ReleaseInjectedLostDeviceForTest(const std::shared_ptr<RenderFramePacket>& interrupted) noexcept
    {
        if (!std::exchange(InjectedDeviceLossForTest, false) || !Device)
            return false;
        auto* const injectedDevice = Device;
        auto* const injectedWindow = NativeWindow;
        const bool injectedWindowClaimed = WindowClaimed;
        GpuDestructionThread = std::this_thread::get_id();
        (void)SDL_WaitForGPUIdle(injectedDevice);
        AbandonLostDeviceResources(interrupted);
        if (injectedWindowClaimed && injectedWindow)
            SDL_ReleaseWindowFromGPUDevice(injectedDevice, injectedWindow);
        WindowClaimed = false;
        SDL_DestroyGPUDevice(injectedDevice);
        Device = nullptr;
        ReleasedInjectedLostDeviceCountForTest.fetch_add(1U, std::memory_order_release);
        return true;
    }
#endif

    void RenderSharedState::DestroyDeviceAndResources(bool abandon, const bool preserveSurfaceEpochs) noexcept
    {
        GpuDestructionThread = std::this_thread::get_id();
        try
        {
            RequireRenderThread("DestroyDeviceAndResources");
            if (!abandon)
            {
                try
                {
                    while (!InFlight.empty())
                        CollectCompletedFrames(true);
                    if (Device)
                        (void)SDL_WaitForGPUIdle(Device);
                }
                catch (...)
                {
                    abandon = true;
                    DeviceLost = true;
                }
            }
            if (abandon)
            {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
                if (!ReleaseInjectedLostDeviceForTest())
#endif
                    AbandonLostDeviceResources();
                for (const auto& surface : AllSurfaceEpochs())
                {
                    surface->ResourcesAvailable.store(false, std::memory_order_release);
                    surface->PublishedTexture.store(nullptr, std::memory_order_release);
                    surface->PublishedDepthAvailable.store(false, std::memory_order_release);
                    if (!preserveSurfaceEpochs)
                        surface->Owner.reset();
                    surface->Width = 0;
                    surface->Height = 0;
                    surface->PublishSurfacePropertiesSnapshot();
                }
                PendingSceneRequests.clear();
                PendingRuntimeUiTrees.clear();
                PendingUiSurfaceTextureBindings.clear();
                CaptureRequests.clear();
                CaptureRuntimeUiCommands.clear();
                ActiveFrame.reset();
                WindowClaimed = false;
                Device = nullptr;
                return;
            }

            for (const auto& surface : AllSurfaceEpochs())
            {
                ReleaseResources(surface->Resources);
                surface->ResourcesAvailable.store(false, std::memory_order_release);
                surface->PublishedTexture.store(nullptr, std::memory_order_release);
                surface->PublishedDepthAvailable.store(false, std::memory_order_release);
                if (!preserveSurfaceEpochs)
                    surface->Owner.reset();
                surface->Width = 0;
                surface->Height = 0;
                surface->PublishSurfacePropertiesSnapshot();
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
            FrameGpuOcclusionReadbacks.clear();
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
                if (frame.ResolvedEditorUi)
                    frame.ResolvedEditorUi->ReleaseGpuTextures(Device, abandon);
                CompleteFrame(frame.Frame, abandon);
            }
            InFlight.clear();
            PendingSceneRequests.clear();
            PendingRuntimeUiTrees.clear();
            PendingUiSurfaceTextureBindings.clear();
            CaptureRequests.clear();
            CaptureRuntimeUiCommands.clear();
            ActiveFrame.reset();
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
            if (SpatialSelectionFallbackBuffer)
                SDL_ReleaseGPUBuffer(Device, SpatialSelectionFallbackBuffer);
            SpatialSelectionFallbackBuffer = nullptr;
            SpatialSelectionFallbackDeviceGeneration = 0;
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
            ReleaseGpuOcclusionPipelines();
            ReleaseSpatialSelectionPipeline();
            VfxPipelineWarmupState.store(GpuVfxPipelineWarmupState::NotStarted, std::memory_order_relaxed);
            if (ToneMapPipeline)
                SDL_ReleaseGPUGraphicsPipeline(Device, ToneMapPipeline);
            ToneMapPipeline = nullptr;
            if (const auto pipeline = std::exchange(RuntimeUiPipeline, nullptr))
                SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
            ReleaseMeshResources(ErrorMesh);
            ReleaseMeshResources(DefaultMesh);
            if (!abandon && WindowClaimed && Device && NativeWindow)
                SDL_ReleaseWindowFromGPUDevice(Device, NativeWindow);
        }
        catch (...)
        {
            DeviceLost = true;
            AbandonLostDeviceResources();
        }
        WindowClaimed = false;
        if (Device && !abandon)
            SDL_DestroyGPUDevice(Device);
        Device = nullptr;
    }
} // namespace Keire::RenderBackend
