#include "Keire/Rendering/RenderSystem.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Log.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Ui/RuntimeUi.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/WindowInternal.h"

#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/BuiltinUnlitShaders.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <future>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Keire
{
    using RenderBackend::LastSdlError;
    using RenderBackend::RenderSharedState;
    using RenderBackend::RenderSurfaceState;
    using RenderBackend::ValidColor;

    class RenderSurface::Impl final
    {
      public:
        explicit Impl(std::shared_ptr<RenderSurfaceState> state) : State(std::move(state)) {}
        ~Impl()
        {
            if (auto owner = State->Owner.lock())
                owner->RetireSurface(*State);
        }
        std::shared_ptr<RenderSurfaceState> State;
    };

    RenderSurface::RenderSurface(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    RenderSurface::~RenderSurface() = default;
    std::string RenderSurface::Name() const { return m_Impl->State->Specification.Name; }
    std::uint32_t RenderSurface::Width() const noexcept { return m_Impl->State->Width; }
    std::uint32_t RenderSurface::Height() const noexcept { return m_Impl->State->Height; }
    RenderSampleCount RenderSurface::SampleCount() const noexcept { return m_Impl->State->ActualSamples; }
    Color RenderSurface::ClearColor() const noexcept { return m_Impl->State->Specification.ClearColor; }
    std::uint64_t RenderSurface::Generation() const noexcept { return m_Impl->State->Generation; }
    bool RenderSurface::Available() const noexcept
    {
        const auto owner = m_Impl->State->Owner.lock();
        return owner && owner->Open && m_Impl->State->Resources.SampledColor;
    }
    bool RenderSurface::SampledDepthAvailable() const noexcept
    {
        const auto owner = m_Impl->State->Owner.lock();
        return owner && owner->Open && m_Impl->State->Resources.SampledDepth;
    }
    GpuOcclusionSurfaceDiagnostics RenderSurface::OcclusionDiagnostics() const noexcept
    {
        return m_Impl->State->GpuOcclusionDiagnostics;
    }

    GpuOcclusionDebugView RenderSurface::OcclusionDebugView() const noexcept
    {
        return m_Impl->State->GpuOcclusionDebugMode;
    }

    std::uint32_t RenderSurface::OcclusionDebugMip() const noexcept { return m_Impl->State->GpuOcclusionDebugMipLevel; }

    void RenderSurface::RequestSize(const std::uint32_t width, const std::uint32_t height)
    {
        if (width < 1 || width > 16384 || height < 1 || height > 16384)
            throw std::invalid_argument("Render surface dimensions must be in the range 1..16384.");
        if (const auto owner = m_Impl->State->Owner.lock())
        {
            owner->RequireOwner("RenderSurface::RequestSize");
            m_Impl->State->RequestedWidth = width;
            m_Impl->State->RequestedHeight = height;
            if (m_Impl->State->FailedWidth != width || m_Impl->State->FailedHeight != height)
            {
                m_Impl->State->FailedWidth = 0;
                m_Impl->State->FailedHeight = 0;
            }
        }
    }

    void RenderSurface::SetClearColor(const Color color)
    {
        if (!ValidColor(color))
            throw std::invalid_argument("Render surface clear color must contain finite values in 0..1.");
        if (const auto owner = m_Impl->State->Owner.lock())
        {
            owner->RequireOwner("RenderSurface::SetClearColor");
            m_Impl->State->Specification.ClearColor = color;
        }
    }

    void RenderSurface::SetOcclusionDebugView(const GpuOcclusionDebugView view, const std::uint32_t mip)
    {
        if (view > GpuOcclusionDebugView::HierarchicalDepth)
            throw std::invalid_argument("RenderSurface GPU occlusion debug view is invalid.");
        const auto owner = m_Impl->State->Owner.lock();
        if (!owner)
            return;
        owner->RequireOwner("RenderSurface::SetOcclusionDebugView");
        m_Impl->State->GpuOcclusionDebugMode = view;
        const auto mipCount = m_Impl->State->GpuOcclusionDiagnostics.PyramidMipCount;
        m_Impl->State->GpuOcclusionDebugMipLevel =
            view == GpuOcclusionDebugView::HierarchicalDepth && mipCount != 0U ? std::min(mip, mipCount - 1U) : 0U;
    }

    class RenderView::Impl final
    {
      public:
        explicit Impl(Ref<RenderSurface> surface) : Surface(std::move(surface)) {}
        Ref<RenderSurface> Surface;
        RenderCamera Camera;
    };

    RenderView::RenderView(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    RenderView::~RenderView() = default;
    Ref<RenderSurface> RenderView::Surface() const noexcept { return m_Impl->Surface; }
    RenderCamera RenderView::Camera() const noexcept { return m_Impl->Camera; }
    void RenderView::SetCamera(const RenderCamera camera)
    {
        if (!Math::IsFinite(camera.View) || !Math::IsFinite(camera.Projection) || !ValidColor(camera.ClearColor))
            throw std::invalid_argument("Render camera contains invalid values.");
        m_Impl->Camera = camera;
    }

    class RenderSystem::Impl final
    {
      public:
        Impl(RenderSpecification specification, const Ref<WindowSystem>& windows, const Ref<Window>& window,
             const Ref<AssetSystem>& assets, const Ref<JobSystem>& jobs, const Ref<StreamingSystem>& streaming)
            : State(std::make_shared<RenderSharedState>(std::move(specification), std::move(windows), std::move(window),
                                                        std::move(assets), std::move(jobs), std::move(streaming)))
        {
        }
        std::shared_ptr<RenderSharedState> State;
    };

    RenderSystem::RenderSystem(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window,
                               Ref<AssetSystem> assets, Ref<JobSystem> jobs, Ref<StreamingSystem> streaming)
        : m_Impl(std::make_unique<Impl>(std::move(specification), std::move(windows), std::move(window),
                                        std::move(assets), std::move(jobs), std::move(streaming)))
    {
    }

    RenderSystem::~RenderSystem() = default;

    Ref<RenderSurface> RenderSystem::CreateSurface(const RenderSurfaceSpecification& specification)
    {
        m_Impl->State->RequireOwner("CreateSurface");
        if (specification.Name.empty() || specification.Name.size() > 128)
            throw std::invalid_argument("Render surface names must contain 1..128 UTF-8 bytes.");
        if (specification.Width < 1 || specification.Width > 16384 || specification.Height < 1 ||
            specification.Height > 16384)
            throw std::invalid_argument("Render surface dimensions must be in the range 1..16384.");
        if (!ValidColor(specification.ClearColor))
            throw std::invalid_argument("Render surface clear color must contain finite values in 0..1.");

        auto state = std::make_shared<RenderSurfaceState>();
        state->Owner = m_Impl->State;
        state->Specification = std::move(specification);
        state->RequestedWidth = state->Specification.Width;
        state->RequestedHeight = state->Specification.Height;
        state->FrameClearColor = state->Specification.ClearColor;
        state->Id = m_Impl->State->NextSurfaceId++;
        m_Impl->State->Surfaces.push_back(state);
        return CreateRef<RenderSurface>(std::make_unique<RenderSurface::Impl>(std::move(state)));
    }

    Ref<RenderView> RenderSystem::CreateView(const RenderSurfaceSpecification& specification)
    {
        return CreateRef<RenderView>(std::make_unique<RenderView::Impl>(CreateSurface(std::move(specification))));
    }

    void RenderSystem::Submit(const SceneRenderRequest& request) { m_Impl->State->Submit(request); }
    void RenderSystem::Submit(SceneRenderRequest&& request) { m_Impl->State->Submit(std::move(request)); }
    void RenderSystem::SubmitRuntimeUi(const Ref<RuntimeUiTree>& tree)
    {
        auto& state = *m_Impl->State;
        state.RequireOwner("SubmitRuntimeUi");
        if (!state.FrameActive)
            throw std::logic_error("Runtime UI submissions are accepted only during an active render frame.");
        state.RuntimeUiCommands.clear();
        if (tree)
        {
            const auto commands = tree->DrawCommands();
            state.RuntimeUiCommands.assign(commands.begin(), commands.end());
        }
    }
    void RenderSystem::RequestGpuVfxPipelineWarmup()
    {
        m_Impl->State->RequireOwner("RequestGpuVfxPipelineWarmup");
        if (m_Impl->State->Specification.Mode == RenderMode::Rendered)
            m_Impl->State->StartGpuVfxPipelineWarmup();
    }
    RenderMode RenderSystem::Mode() const noexcept { return m_Impl->State->Specification.Mode; }
    RenderCapabilities RenderSystem::Capabilities() const noexcept
    {
        const bool rendered = m_Impl->State->Specification.Mode == RenderMode::Rendered && m_Impl->State->Device;
        return {.CpuVfxSimulation = true,
                .GpuVfxSimulation = rendered,
                .TransparentPass = rendered,
                .DynamicSpritePackets = rendered,
                .TexturedSpritePackets = false,
                .DynamicMeshPackets = rendered,
                .SampledResolvedDepth = rendered && m_Impl->State->ShadowDepthFormat != SDL_GPU_TEXTUREFORMAT_INVALID,
                .GpuDepthCollision = false,
                .GpuOcclusionCulling = rendered && m_Impl->State->GpuOcclusionCapability};
    }
    RenderStatistics RenderSystem::Statistics() const noexcept
    {
        auto result = m_Impl->State->Statistics;
        const auto warmup = m_Impl->State->VfxPipelineWarmupState.load(std::memory_order_acquire);
        result.VfxPipelineWarmupPending = warmup == RenderBackend::GpuVfxPipelineWarmupState::Compiling;
        result.VfxPipelinesReady = warmup == RenderBackend::GpuVfxPipelineWarmupState::Ready;
        result.VfxPipelineWarmupMilliseconds =
            static_cast<float>(m_Impl->State->VfxPipelineWarmupMicroseconds.load(std::memory_order_relaxed)) / 1000.0F;
        return result;
    }

    FrameGraphSnapshot RenderSystem::CaptureFrameGraph() const
    {
        auto& state = *m_Impl->State;
        state.RequireOwner("CaptureFrameGraph");
        FrameGraphSnapshot snapshot;
        snapshot.Frame = state.Statistics.Frame;
        snapshot.FenceRetiredBytes = state.Statistics.FenceRetiredBytes;

        std::uint64_t largestPixels = 0;
        for (const auto& surface : state.LiveSurfaces())
            largestPixels = std::max(largestPixels, static_cast<std::uint64_t>(surface->Width) * surface->Height);

        const auto resources = state.SceneFrameGraph.Graph.Resources();
        const auto passes = state.SceneFrameGraph.Graph.Passes();
        const auto& compiled = state.SceneFrameGraph.Compiled;
        snapshot.Resources.reserve(resources.size());
        std::vector<std::uint64_t> aliasBytes(compiled.TransientAllocations.size());
        for (std::uint32_t index = 0; index < resources.size(); ++index)
        {
            const auto& resource = resources[index];
            const auto& lifetime = compiled.Lifetimes[index];
            const auto physical = compiled.PhysicalResources[index];
            auto estimatedBytes = resource.SizeBytes;
            if (estimatedBytes == 0 && resource.Kind == RenderBackend::FrameGraphResourceKind::Texture)
            {
                const auto bytesPerPixel = resource.Name.find("HDR") != std::string::npos ? 8ULL : 4ULL;
                estimatedBytes = largestPixels * bytesPerPixel;
            }
            snapshot.Resources.push_back({index, resource.Name,
                                          static_cast<FrameGraphSnapshotResourceKind>(resource.Kind),
                                          lifetime.FirstPass, lifetime.LastPass, physical, resource.CompatibilityKey,
                                          estimatedBytes, resource.Imported, lifetime.Used});
            if (!resource.Imported && lifetime.Used)
            {
                snapshot.TheoreticalUnaliasedBytes += estimatedBytes;
                if (physical < aliasBytes.size())
                    aliasBytes[physical] = std::max(aliasBytes[physical], estimatedBytes);
            }
        }
        for (const auto bytes : aliasBytes)
            snapshot.ActiveTransientBytes += bytes;
        snapshot.SavedAliasingBytes = snapshot.TheoreticalUnaliasedBytes -
                                      std::min(snapshot.TheoreticalUnaliasedBytes, snapshot.ActiveTransientBytes);

        snapshot.Passes.reserve(compiled.Order.size());
        for (std::uint32_t order = 0; order < compiled.Order.size(); ++order)
        {
            const auto passIndex = compiled.Order[order].Value;
            const auto& pass = passes[passIndex];
            FrameGraphSnapshotPass captured;
            captured.Index = passIndex;
            captured.Order = order;
            captured.Name = pass.Name;
            captured.Kind = static_cast<FrameGraphSnapshotPassKind>(pass.Kind);
            for (const auto resource : pass.Reads)
                captured.Reads.push_back(resource.Value);
            for (const auto resource : pass.Writes)
                captured.Writes.push_back(resource.Value);
            for (const auto& transition : compiled.Execution[order].Transitions)
                captured.Transitions.push_back({transition.Resource.Value,
                                                static_cast<FrameGraphSnapshotResourceState>(transition.Before),
                                                static_cast<FrameGraphSnapshotResourceState>(transition.After)});
            snapshot.Passes.push_back(std::move(captured));
        }

        state.Statistics.ActiveTransientBytes = snapshot.ActiveTransientBytes;
        state.Statistics.TheoreticalUnaliasedBytes = snapshot.TheoreticalUnaliasedBytes;
        state.Statistics.SavedAliasingBytes = snapshot.SavedAliasingBytes;
        return snapshot;
    }
    bool RenderSystem::IsOpen() const noexcept { return m_Impl->State->Open; }
    void RenderSystem::Close() noexcept { m_Impl->State->Close(); }

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

    SDL_GPUTexture* RenderSystemInternalAccess::Texture(const RenderSurface& surface) noexcept
    {
        return surface.m_Impl->State->Resources.SampledColor;
    }

    std::vector<std::uint8_t> RenderSystemInternalAccess::ReadbackRGBA8(RenderSystem& renderer,
                                                                        const RenderSurface& surface)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("ReadbackRGBA8");
        const auto& surfaceState = *surface.m_Impl->State;
        const auto owner = surfaceState.Owner.lock();
        if (owner.get() != &renderState)
            throw std::invalid_argument("Render surface belongs to another renderer.");
        if (!surfaceState.Resources.SampledColor || surfaceState.Width == 0 || surfaceState.Height == 0)
            throw std::logic_error("Render surface is not available for readback.");

        const std::uint64_t byteSize64 =
            static_cast<std::uint64_t>(surfaceState.Width) * static_cast<std::uint64_t>(surfaceState.Height) * 4ULL;
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
                surfaceState.Resources.SampledColor, 0, 0, 0, 0, 0, surfaceState.Width, surfaceState.Height, 1};
            const SDL_GPUTextureTransferInfo destination{transfer, 0, surfaceState.Width, surfaceState.Height};
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
            std::vector<std::uint8_t> pixels(byteSize);
            std::memcpy(pixels.data(), mapped, byteSize);
            SDL_UnmapGPUTransferBuffer(renderState.Device, transfer);
            SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            return pixels;
        }
        catch (...)
        {
            if (fence)
                SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            throw;
        }
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
            !surfaceState.Resources.DirectionalShadow || layer >= surfaceState.Resources.DirectionalShadowLayers)
            throw std::logic_error("Directional shadow surface is not available for D32 readback.");

        const auto resolution = surfaceState.Resources.DirectionalShadowResolution;
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
                surfaceState.Resources.DirectionalShadow, 0, layer, 0, 0, 0, resolution, resolution, 1};
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
        if (renderState.ShadowDepthFormat != SDL_GPU_TEXTUREFORMAT_D32_FLOAT || !surfaceState.Resources.LocalShadow ||
            layer >= surfaceState.Resources.LocalShadowLayers)
            throw std::logic_error("Local shadow surface is not available for D32 readback.");

        const auto resolution = surfaceState.Resources.LocalShadowResolution;
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
                surfaceState.Resources.LocalShadow, 0, layer, 0, 0, 0, resolution, resolution, 1};
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
            if (fence)
                SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            throw;
        }
    }

    void RenderSystemInternalAccess::InjectDeviceLoss(RenderSystem& renderer)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("InjectDeviceLoss");
        if (renderState.Specification.Mode != RenderMode::Rendered)
            throw std::logic_error("Device-loss injection requires a rendered backend.");
        renderState.InjectDeviceLossAtNextFrame.store(true, std::memory_order_release);
    }

    std::uint32_t RenderSystemInternalAccess::SaturateRendererQueue(RenderSystem& renderer)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("SaturateRendererQueue");
        if (!renderState.RenderThread.joinable())
            throw std::logic_error("Queue saturation requires an active renderer thread.");

        std::promise<void> workerStarted;
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

        std::jthread first(
            [&]
            {
                produce(
                    [&]
                    {
                        workerStarted.set_value();
                        release.wait();
                    });
            });
        workerStarted.get_future().wait();
        std::jthread second([&] { produce([] {}); });
        std::jthread third([&] { produce([] {}); });

        bool saturated = false;
        std::uint32_t highWaterMark = 0;
        {
            std::unique_lock lock(renderState.RenderQueueMutex);
            saturated = renderState.RenderQueueReady.wait_for(lock, std::chrono::seconds(2),
                                                              [&] { return renderState.RenderQueue.size() == 2; });
            highWaterMark = renderState.Statistics.RendererQueueHighWaterMark;
        }
        releaseWorker.set_value();
        first.join();
        second.join();
        third.join();
        if (producerFailure)
            std::rethrow_exception(producerFailure);
        if (!saturated)
            throw std::runtime_error("Renderer queue did not reach its bounded capacity.");
        return highWaterMark;
    }

    void RenderSystemInternalAccess::RequestSurfaceSize(RenderSurface& surface, const std::uint32_t width,
                                                        const std::uint32_t height)
    {
        if (width > 16384 || height > 16384)
            throw std::invalid_argument("Test surface dimensions must be in the range 0..16384.");
        if (const auto owner = surface.m_Impl->State->Owner.lock())
        {
            owner->RequireOwner("RequestSurfaceSize");
            surface.m_Impl->State->RequestedWidth = width;
            surface.m_Impl->State->RequestedHeight = height;
            surface.m_Impl->State->FailedWidth = 0;
            surface.m_Impl->State->FailedHeight = 0;
        }
    }

    void RenderSystemInternalAccess::SetPresentationSurface(RenderSystem& renderer, const Ref<RenderSurface>& surface)
    {
        auto& rendererState = renderer.m_Impl->State;
        rendererState->RequireOwner("SetPresentationSurface");
        if (!surface)
        {
            rendererState->PresentationSurface.reset();
            return;
        }
        const auto surfaceOwner = surface->m_Impl->State->Owner.lock();
        if (surfaceOwner != rendererState)
            throw std::invalid_argument("A presentation surface must belong to its renderer.");
        rendererState->PresentationSurface = surface->m_Impl->State;
    }

    std::size_t RenderSystemInternalAccess::RuntimeUiCommandCount(const RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->RuntimeUiCommands.size();
    }

    void* RenderSystemInternalAccess::SurfaceState(RenderSurface& surface) noexcept
    {
        return surface.m_Impl->State.get();
    }

    void RenderSystemInternalAccess::WaitIdle(RenderSystem& renderer) noexcept
    {
        if (renderer.m_Impl->State->Device)
            (void)SDL_WaitForGPUIdle(renderer.m_Impl->State->Device);
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
