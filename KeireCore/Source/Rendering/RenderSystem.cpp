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

#include "KeireInternal/Rendering/GpuVisibilityCandidateInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderSystemFacadeInternal.h"

#include "Keire/BuiltinUnlitShaders.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <future>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Keire
{
    using RenderBackend::LastSdlError;
    using RenderBackend::RenderSharedState;
    using RenderBackend::RenderSurfaceEpochLease;
    using RenderBackend::RenderSurfaceState;
    using RenderBackend::ValidColor;

    namespace
    {
        [[nodiscard]] std::string IncidentField(const std::string_view value)
        {
            constexpr std::size_t maximumFieldBytes = 160U;
            std::string result(value.substr(0U, maximumFieldBytes));
            std::ranges::replace_if(result, [](const unsigned char character) { return character < 0x20U; }, '?');
            return result.empty() ? "unknown" : result;
        }

        [[nodiscard]] std::string IncidentMessage(const GpuDeviceLossDiagnostic& diagnostic)
        {
            std::ostringstream stream;
            stream << "GPU incident [operation=" << IncidentField(diagnostic.Operation)
                   << ", backend=" << IncidentField(diagnostic.Backend)
                   << ", adapter=" << IncidentField(diagnostic.Adapter) << ", frame=" << diagnostic.Frame
                   << ", deviceGeneration=" << diagnostic.DeviceGeneration
                   << ", driver=" << IncidentField(diagnostic.DriverName)
                   << ", driverVersion=" << IncidentField(diagnostic.DriverVersion)
                   << ", recoveryAttempt=" << diagnostic.RecoveryAttempt << ']';
            return stream.str();
        }
    } // namespace

    GpuDeviceLostError::GpuDeviceLostError(GpuDeviceLossDiagnostic diagnostic)
        : std::runtime_error(IncidentMessage(diagnostic)), m_Diagnostic(std::move(diagnostic))
    {
    }

    const GpuDeviceLossDiagnostic& GpuDeviceLostError::Diagnostic() const noexcept { return m_Diagnostic; }

    RenderSurface::Impl::Impl(std::shared_ptr<RenderSurfaceState> state) : State(std::move(state)) {}

    RenderSurface::Impl::~Impl()
    {
        if (auto owner = State->Owner.lock())
            owner->RequestSurfaceRetirement(State);
    }

    RenderSurface::RenderSurface(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    RenderSurface::~RenderSurface() = default;
    std::string RenderSurface::Name() const { return m_Impl->State->Specification.Name; }
    std::uint32_t RenderSurface::Width() const noexcept { return m_Impl->State->SurfacePropertiesSnapshot().Width; }
    std::uint32_t RenderSurface::Height() const noexcept { return m_Impl->State->SurfacePropertiesSnapshot().Height; }
    RenderSampleCount RenderSurface::SampleCount() const noexcept
    {
        return m_Impl->State->SurfacePropertiesSnapshot().SampleCount;
    }
    Color RenderSurface::ClearColor() const noexcept { return m_Impl->State->Specification.ClearColor; }
    std::uint64_t RenderSurface::Generation() const noexcept { return m_Impl->State->Epoch; }
    bool RenderSurface::Available() const noexcept
    {
        const auto owner = m_Impl->State->Owner.lock();
        return owner && owner->Open &&
               owner->DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Running &&
               m_Impl->State->ResourcesAvailable.load(std::memory_order_acquire);
    }
    bool RenderSurface::SampledDepthAvailable() const noexcept
    {
        const auto owner = m_Impl->State->Owner.lock();
        return owner && owner->Open && m_Impl->State->PublishedDepthAvailable.load(std::memory_order_acquire);
    }
    GpuOcclusionSurfaceDiagnostics RenderSurface::OcclusionDiagnostics() const noexcept
    {
        return m_Impl->State->GpuOcclusionDiagnosticsSnapshot();
    }

    GpuOcclusionDebugView RenderSurface::OcclusionDebugView() const noexcept
    {
        return m_Impl->State->GpuOcclusionDebugMode.load(std::memory_order_acquire);
    }

    std::uint32_t RenderSurface::OcclusionDebugMip() const noexcept
    {
        return m_Impl->State->GpuOcclusionDebugMipLevel.load(std::memory_order_acquire);
    }

    void RenderSurface::RequestSize(const std::uint32_t width, const std::uint32_t height)
    {
        if (width < 1 || width > 16384 || height < 1 || height > 16384)
            throw std::invalid_argument("Render surface dimensions must be in the range 1..16384.");
        if (const auto owner = m_Impl->State->Owner.lock())
        {
            owner->RequireOwner("RenderSurface::RequestSize");
            if (m_Impl->State->RequestedWidth == width && m_Impl->State->RequestedHeight == height)
                return;
            m_Impl->State =
                owner->CreateSurfaceEpoch(m_Impl->State, width, height, m_Impl->State->Specification.SampleCount);
        }
    }

    void RenderSurface::RequestSampleCount(const RenderSampleCount sampleCount)
    {
        switch (sampleCount)
        {
        case RenderSampleCount::One:
        case RenderSampleCount::Two:
        case RenderSampleCount::Four:
        case RenderSampleCount::Eight:
            break;
        default:
            throw std::invalid_argument("Render surface sample count must be 1, 2, 4, or 8.");
        }
        if (const auto owner = m_Impl->State->Owner.lock())
        {
            owner->RequireOwner("RenderSurface::RequestSampleCount");
            if (m_Impl->State->Specification.SampleCount == sampleCount)
                return;
            m_Impl->State = owner->CreateSurfaceEpoch(m_Impl->State, m_Impl->State->RequestedWidth,
                                                      m_Impl->State->RequestedHeight, sampleCount);
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
        const auto mipCount = m_Impl->State->GpuOcclusionDiagnosticsSnapshot().PyramidMipCount;
        const auto selectedMip =
            view == GpuOcclusionDebugView::HierarchicalDepth && mipCount != 0U ? std::min(mip, mipCount - 1U) : 0U;
        m_Impl->State->GpuOcclusionDebugMipLevel.store(selectedMip, std::memory_order_release);
        m_Impl->State->GpuOcclusionDebugMode.store(view, std::memory_order_release);
    }

    RenderView::Impl::Impl(Ref<RenderSurface> surface) : Surface(std::move(surface)) {}
    RenderView::Impl::~Impl() = default;

    RenderView::RenderView(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    RenderView::~RenderView() = default;
    Ref<RenderSurface> RenderView::Surface() const noexcept { return m_Impl->Surface; }
    RenderCamera RenderView::Camera() const noexcept { return m_Impl->Camera; }
    void RenderView::SetCamera(const RenderCamera camera)
    {
        if (!Math::IsFinite(camera.View) || !Math::IsFinite(camera.Projection) || !ValidColor(camera.ClearColor))
            throw std::invalid_argument("Render camera contains invalid values.");
        if (!std::isfinite(camera.NearPlane) || !std::isfinite(camera.FarPlane) || camera.NearPlane <= 0.0F ||
            camera.FarPlane <= camera.NearPlane || camera.FarPlane > 10'000'000.0F)
        {
            throw std::invalid_argument("Render camera clip planes must satisfy 0 < near < far <= 10000000.");
        }
        m_Impl->Camera = camera;
    }

    RenderSystem::Impl::Impl(RenderSpecification specification, const Ref<WindowSystem>& windows,
                             const Ref<Window>& window, const Ref<AssetSystem>& assets, const Ref<JobSystem>& jobs,
                             const Ref<StreamingSystem>& streaming)
        : State(std::make_shared<RenderSharedState>(std::move(specification), windows, window, assets, jobs, streaming))
    {
    }

    RenderSystem::Impl::~Impl() = default;

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
        {
            std::scoped_lock lock(m_Impl->State->SurfaceMutex);
            state->Id = m_Impl->State->NextSurfaceId++;
            state->Lifetime = std::make_shared<RenderSurfaceEpochLease>(state->Id, state->Epoch);
            m_Impl->State->Surfaces.push_back({state, true});
        }
        return CreateRef<RenderSurface>(std::make_unique<RenderSurface::Impl>(std::move(state)));
    }

    Ref<RenderView> RenderSystem::CreateView(const RenderSurfaceSpecification& specification)
    {
        return CreateRef<RenderView>(std::make_unique<RenderView::Impl>(CreateSurface(std::move(specification))));
    }

    void RenderSystem::Submit(const SceneRenderRequest& request) { m_Impl->State->Submit(request); }
    void RenderSystem::Submit(SceneRenderRequest&& request) { m_Impl->State->Submit(std::move(request)); }
    void RenderSystem::SubmitRuntimeUi(const Ref<RuntimeUiTree>& tree) { SubmitRuntimeUiTarget({.Tree = tree}); }

    void RenderSystem::SubmitRuntimeUiTarget(RuntimeUiRenderSubmission submission)
    {
        auto& state = *m_Impl->State;
        state.RequireOwner("SubmitRuntimeUi");
        if (!state.FrameActive)
            throw std::logic_error("Runtime UI submissions are accepted only during an active render frame.");
        if (!submission.Tree)
            return;
        if (submission.Root && !submission.Tree->Exists(submission.Root))
            throw std::invalid_argument("Runtime UI submission root does not exist in the submitted tree.");
        if (state.PendingRuntimeUiSubmissions.size() >= 256U)
            throw std::length_error("Runtime UI submissions exceed the per-frame panel bound of 256.");

        RenderBackend::RenderSurfaceToken surface;
        switch (submission.Target)
        {
        case RuntimeUiRenderTarget::ScreenOverlay:
            break;
        case RuntimeUiRenderTarget::CameraOverlay:
        {
            if (!submission.View || !submission.View->Surface())
                throw std::invalid_argument("Camera-overlay runtime UI requires its authored camera render view.");
            if (!std::isfinite(submission.Viewport.X) || !std::isfinite(submission.Viewport.Y) ||
                submission.Viewport.X <= 0.0F || submission.Viewport.Y <= 0.0F)
            {
                throw std::invalid_argument("Camera-overlay runtime UI requires a finite positive viewport.");
            }
            auto surfaceLease = std::static_pointer_cast<RenderBackend::RenderSurfaceState>(
                RenderSystemInternalAccess::SurfaceLease(*submission.View->Surface()));
            const auto owner =
                surfaceLease ? surfaceLease->Owner.lock() : std::shared_ptr<RenderBackend::RenderSharedState>{};
            if (owner.get() != &state)
                throw std::invalid_argument("Camera-overlay runtime UI render view belongs to another renderer.");
            surface = state.CaptureSurfaceToken(surfaceLease);
            break;
        }
        case RuntimeUiRenderTarget::RenderTexture:
            if (!submission.RenderTexture)
                throw std::invalid_argument("Runtime UI RenderTexture targets require a non-empty logical target ID.");
            if (!std::isfinite(submission.ReferenceResolution.X) || !std::isfinite(submission.ReferenceResolution.Y) ||
                submission.ReferenceResolution.X < 1.0F || submission.ReferenceResolution.Y < 1.0F ||
                submission.ReferenceResolution.X > 16384.0F || submission.ReferenceResolution.Y > 16384.0F)
            {
                throw std::invalid_argument(
                    "Runtime UI RenderTexture reference resolution must be finite and within 1..16384 per axis.");
            }
            if (std::ranges::none_of(state.PendingRuntimeUiSubmissions,
                                     [&submission](const auto& pending)
                                     {
                                         return pending.Submission.Target == RuntimeUiRenderTarget::RenderTexture &&
                                                pending.Submission.RenderTexture == submission.RenderTexture;
                                     }))
            {
                std::vector<AssetId> uniqueTargets;
                for (const auto& pending : state.PendingRuntimeUiSubmissions)
                {
                    if (pending.Submission.Target != RuntimeUiRenderTarget::RenderTexture ||
                        std::ranges::find(uniqueTargets, pending.Submission.RenderTexture) != uniqueTargets.end())
                    {
                        continue;
                    }
                    uniqueTargets.push_back(pending.Submission.RenderTexture);
                }
                if (uniqueTargets.size() >= 32U)
                {
                    throw std::length_error(
                        "Runtime UI submissions exceed the per-frame RenderTexture target bound of 32.");
                }
            }
            break;
        case RuntimeUiRenderTarget::WorldSurface:
        {
            if (!submission.View || !submission.View->Surface())
                throw std::invalid_argument("World-surface runtime UI requires a render view and surface.");
            if (!Math::IsFinite(submission.World) || !std::isfinite(submission.Viewport.X) ||
                !std::isfinite(submission.Viewport.Y) || submission.Viewport.X <= 0.0F ||
                submission.Viewport.Y <= 0.0F || !std::isfinite(submission.ReferenceResolution.X) ||
                !std::isfinite(submission.ReferenceResolution.Y) || submission.ReferenceResolution.X <= 0.0F ||
                submission.ReferenceResolution.Y <= 0.0F || !std::isfinite(submission.Pivot.X) ||
                !std::isfinite(submission.Pivot.Y) || !std::isfinite(submission.WorldUnitsPerPixel.X) ||
                !std::isfinite(submission.WorldUnitsPerPixel.Y) || submission.WorldUnitsPerPixel.X <= 0.0F ||
                submission.WorldUnitsPerPixel.Y <= 0.0F)
            {
                throw std::invalid_argument("World-surface runtime UI contains invalid projection values.");
            }
            auto surfaceLease = std::static_pointer_cast<RenderBackend::RenderSurfaceState>(
                RenderSystemInternalAccess::SurfaceLease(*submission.View->Surface()));
            const auto owner =
                surfaceLease ? surfaceLease->Owner.lock() : std::shared_ptr<RenderBackend::RenderSharedState>{};
            if (owner.get() != &state)
                throw std::invalid_argument("World-surface runtime UI render view belongs to another renderer.");
            if (submission.DepthTest && !surfaceLease->Specification.Depth)
                throw std::invalid_argument("Depth-tested world-surface runtime UI requires a depth-enabled surface.");
            surface = state.CaptureSurfaceToken(surfaceLease);
            break;
        }
        }
        state.PendingRuntimeUiSubmissions.push_back({.Submission = std::move(submission),
                                                     .Surface = std::move(surface),
                                                     .Sequence = state.NextRuntimeUiSubmissionSequence++});
    }
    void RenderSystem::RequestGpuVfxPipelineWarmup()
    {
        m_Impl->State->RequireOwner("RequestGpuVfxPipelineWarmup");
        if (m_Impl->State->Specification.Mode == RenderMode::Rendered)
            m_Impl->State->StartGpuVfxPipelineWarmup();
    }
    RenderMode RenderSystem::Mode() const noexcept { return m_Impl->State->Specification.Mode; }
    RenderFeatureCapabilities RenderSystem::FeatureCapabilities() const noexcept
    {
        const bool rendered =
            m_Impl->State->Specification.Mode == RenderMode::Rendered &&
            m_Impl->State->DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Running;
        const bool deferredHybrid = rendered && m_Impl->State->DeferredCapability.load(std::memory_order_acquire);
        const bool msaa2 = rendered && m_Impl->State->Msaa2Capability.load(std::memory_order_acquire);
        const bool msaa4 = rendered && m_Impl->State->Msaa4Capability.load(std::memory_order_acquire);
        return {.DeferredHybrid = deferredHybrid,
                .BakedGlobalIllumination = rendered,
                .RealtimeGlobalIllumination = rendered,
                .IrradynGlobalIllumination = deferredHybrid,
                .IrradynRequiresDeferredHybrid = true,
                .Fxaa = rendered,
                .TemporalAntiAliasing = rendered,
                .Msaa2 = msaa2,
                .Msaa4 = msaa4,
                .DeferredMultisample = deferredHybrid && (msaa2 || msaa4),
                .DynamicResolution = rendered};
    }
    RenderCapabilities RenderSystem::Capabilities() const noexcept
    {
        const bool rendered =
            m_Impl->State->Specification.Mode == RenderMode::Rendered &&
            m_Impl->State->DeviceLifecycle.load(std::memory_order_acquire) == RenderDeviceState::Running;
        const bool gpuOcclusion = rendered && m_Impl->State->GpuOcclusionCapability.load(std::memory_order_acquire);
        const bool deferredHybrid = rendered && m_Impl->State->DeferredCapability.load(std::memory_order_acquire);
        const auto features = FeatureCapabilities();
        const auto visibility = RenderBackend::AdvertisedGpuVisibilityCapabilities(gpuOcclusion);
        return {.CpuVfxSimulation = true,
                .GpuVfxSimulation = rendered,
                .DeferredHybrid = deferredHybrid,
                .BakedGlobalIllumination = rendered,
                .RealtimeGlobalIllumination = rendered,
                .IrradynGlobalIllumination = deferredHybrid,
                .Fxaa = features.Fxaa,
                .TemporalAntiAliasing = features.TemporalAntiAliasing,
                .Msaa2 = features.Msaa2,
                .Msaa4 = features.Msaa4,
                .DeferredMultisample = features.DeferredMultisample,
                .DynamicResolution = features.DynamicResolution,
                .TransparentPass = rendered,
                .DynamicSpritePackets = rendered,
                .TexturedSpritePackets = false,
                .RuntimeUiWorldSurfaces = rendered,
                .RuntimeUiRenderTextures = rendered,
                .DynamicMeshPackets = rendered,
                .SampledResolvedDepth = rendered,
                .GpuDepthCollision = rendered,
                .GpuOcclusionCulling = visibility.Culling,
                .GpuOcclusionStaticMeshes = visibility.StaticMeshes,
                .GpuOcclusionSkinnedMeshes = visibility.SkinnedMeshes,
                .GpuOcclusionMeshVfx = visibility.MeshVfx,
                .GpuOcclusionVfxVisibilityMasks = visibility.VfxVisibilityMasks,
                .GpuOcclusionLocalLightMasks = visibility.LocalLightMasks,
                .GpuOcclusionSpatialVolumeMasks = visibility.SpatialVolumeMasks};
    }
    RenderStatistics RenderSystem::Statistics() const noexcept
    {
        RenderStatistics result;
        {
            std::scoped_lock lock(m_Impl->State->PublicationMutex);
            result = m_Impl->State->PublishedStatistics;
        }
        const auto warmup = m_Impl->State->VfxPipelineWarmupState.load(std::memory_order_acquire);
        result.VfxPipelineWarmupPending = warmup == RenderBackend::GpuVfxPipelineWarmupState::Compiling;
        result.VfxPipelinesReady = warmup == RenderBackend::GpuVfxPipelineWarmupState::Ready;
        result.VfxPipelineWarmupMilliseconds =
            static_cast<float>(m_Impl->State->VfxPipelineWarmupMicroseconds.load(std::memory_order_relaxed)) / 1000.0F;
        result.OutstandingFrames = m_Impl->State->OutstandingFrames.load(std::memory_order_relaxed);
        result.FramesInFlightHighWaterMark = m_Impl->State->OutstandingHighWaterMark.load(std::memory_order_relaxed);
        result.AcceptedFrames = m_Impl->State->AcceptedFrameCount.load(std::memory_order_relaxed);
        result.PresentedFrames = m_Impl->State->PresentedFrameCount.load(std::memory_order_relaxed);
        result.RetiredFrames = m_Impl->State->RetiredFrameCount.load(std::memory_order_relaxed);
        result.CancelledFrames = m_Impl->State->CancelledFrameCount.load(std::memory_order_relaxed);
        result.LastAcceptedFrame = m_Impl->State->LastAcceptedFrameId.load(std::memory_order_relaxed);
        result.LastPresentedFrame = m_Impl->State->LastPresentedFrameId.load(std::memory_order_relaxed);
        result.LastRetiredFrame = m_Impl->State->LastRetiredFrameId.load(std::memory_order_relaxed);
        result.RendererQueueHighWaterMark = m_Impl->State->RenderQueueHighWaterMark.load(std::memory_order_relaxed);
        return result;
    }

    std::vector<RenderFrameTimeline> RenderSystem::RecentFrameTimelines() const
    {
        std::scoped_lock lock(m_Impl->State->PublicationMutex);
        return {m_Impl->State->PublishedTimelines.begin(), m_Impl->State->PublishedTimelines.end()};
    }

    RenderDeviceIdentity RenderSystem::DeviceIdentity() const
    {
        std::scoped_lock lock(m_Impl->State->DeviceIdentityMutex);
        return m_Impl->State->DeviceIdentitySnapshot;
    }

    RenderDeviceState RenderSystem::DeviceState() const noexcept
    {
        return m_Impl->State->DeviceLifecycle.load(std::memory_order_acquire);
    }

    std::optional<GpuDeviceLossDiagnostic> RenderSystem::LastDeviceLoss() const
    {
        std::scoped_lock lock(m_Impl->State->FailureMutex);
        return m_Impl->State->LastDeviceLossDiagnostic;
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

        const auto& frameGraph = state.DeferredCapability.load(std::memory_order_acquire)
                                     ? state.DeferredSceneFrameGraph
                                     : state.SceneFrameGraph;
        const auto resources = frameGraph.Graph.Resources();
        const auto passes = frameGraph.Graph.Passes();
        const auto& compiled = frameGraph.Compiled;
        snapshot.Resources.reserve(resources.size());
        std::vector<std::uint64_t> aliasBytes(compiled.TransientAllocations.size());
        const auto textureBytesPerPixel = [](const RenderBackend::FrameGraphTextureFormat format) noexcept
        {
            switch (format)
            {
            case RenderBackend::FrameGraphTextureFormat::Rgba16Float:
                return 8ULL;
            case RenderBackend::FrameGraphTextureFormat::Rg16Float:
            case RenderBackend::FrameGraphTextureFormat::Rgba8Unorm:
            case RenderBackend::FrameGraphTextureFormat::Rgba8Srgb:
            case RenderBackend::FrameGraphTextureFormat::R32Float:
            case RenderBackend::FrameGraphTextureFormat::D32Float:
                return 4ULL;
            case RenderBackend::FrameGraphTextureFormat::Undefined:
                return 0ULL;
            }
            return 0ULL;
        };
        for (std::uint32_t index = 0; index < resources.size(); ++index)
        {
            const auto& resource = resources[index];
            const auto& lifetime = compiled.Lifetimes[index];
            const auto physical = compiled.PhysicalResources[index];
            auto estimatedBytes = resource.SizeBytes;
            if (estimatedBytes == 0 && resource.Kind == RenderBackend::FrameGraphResourceKind::Texture)
            {
                auto bytesPerPixel = textureBytesPerPixel(resource.Texture.Format);
                if (bytesPerPixel == 0U)
                    bytesPerPixel = resource.Name.find("HDR") != std::string::npos ? 8ULL : 4ULL;
                const auto numerator = static_cast<long double>(resource.Texture.WidthScaleNumerator) *
                                       resource.Texture.HeightScaleNumerator;
                const auto denominator = static_cast<long double>(resource.Texture.WidthScaleDenominator) *
                                         resource.Texture.HeightScaleDenominator;
                const auto scaledBytes = static_cast<long double>(largestPixels) * bytesPerPixel * numerator /
                                         denominator * resource.Texture.SampleCount;
                estimatedBytes = static_cast<std::uint64_t>(
                    std::min(scaledBytes, static_cast<long double>(std::numeric_limits<std::uint64_t>::max())));
            }
            FrameGraphSnapshotResource captured;
            captured.Index = index;
            captured.Name = resource.Name;
            captured.Kind = static_cast<FrameGraphSnapshotResourceKind>(resource.Kind);
            captured.FirstPass = lifetime.FirstPass;
            captured.LastPass = lifetime.LastPass;
            captured.PhysicalAliasSlot = physical;
            captured.CompatibilityKey = resource.CompatibilityKey;
            captured.EstimatedBytes = estimatedBytes;
            captured.Imported = resource.Imported;
            captured.Used = lifetime.Used;
            captured.TextureFormat = static_cast<FrameGraphSnapshotTextureFormat>(resource.Texture.Format);
            captured.Usage = static_cast<FrameGraphSnapshotResourceUsage>(resource.Texture.Usage);
            captured.SampleCount = resource.Texture.SampleCount;
            captured.WidthScaleNumerator = resource.Texture.WidthScaleNumerator;
            captured.WidthScaleDenominator = resource.Texture.WidthScaleDenominator;
            captured.HeightScaleNumerator = resource.Texture.HeightScaleNumerator;
            captured.HeightScaleDenominator = resource.Texture.HeightScaleDenominator;
            snapshot.Resources.push_back(std::move(captured));
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
    void RenderSystem::Flush() { m_Impl->State->Flush(); }
    void RenderSystem::Close() noexcept { m_Impl->State->Close(); }
} // namespace Keire
