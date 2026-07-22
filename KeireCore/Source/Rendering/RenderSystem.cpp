#include "Keire/Rendering/RenderSystem.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Log.h"
#include "Keire/Scenes/Scene.h"

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
        Impl(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window, Ref<AssetSystem> assets)
            : State(std::make_shared<RenderSharedState>(std::move(specification), std::move(windows), std::move(window),
                                                        std::move(assets)))
        {
        }
        std::shared_ptr<RenderSharedState> State;
    };

    RenderSystem::RenderSystem(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window,
                               Ref<AssetSystem> assets)
        : m_Impl(std::make_unique<Impl>(std::move(specification), std::move(windows), std::move(window),
                                        std::move(assets)))
    {
    }

    RenderSystem::~RenderSystem() = default;

    Ref<RenderSurface> RenderSystem::CreateSurface(RenderSurfaceSpecification specification)
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

    Ref<RenderView> RenderSystem::CreateView(RenderSurfaceSpecification specification)
    {
        return CreateRef<RenderView>(std::make_unique<RenderView::Impl>(CreateSurface(std::move(specification))));
    }

    void RenderSystem::Submit(SceneRenderRequest request) { m_Impl->State->Submit(std::move(request)); }
    RenderMode RenderSystem::Mode() const noexcept { return m_Impl->State->Specification.Mode; }
    RenderStatistics RenderSystem::Statistics() const noexcept { return m_Impl->State->Statistics; }
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
