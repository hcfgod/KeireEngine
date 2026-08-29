#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <array>

#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/Log.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <type_traits>

namespace Keire::RenderBackend
{
    namespace
    {
        [[nodiscard]] bool ValidGlobalMaterialProperty(const MaterialPropertyValue& value) noexcept
        {
            return std::visit(
                [](const auto& property) noexcept
                {
                    using Value = std::decay_t<decltype(property)>;
                    if constexpr (std::is_same_v<Value, AssetId>)
                        return true;
                    else if constexpr (std::is_same_v<Value, float>)
                        return std::isfinite(property);
                    else
                        return Math::IsFinite(property);
                },
                value);
        }
    } // namespace

    void RenderSharedState::CollectCompletedFrames() { CollectCompletedFrames(false); }

    void RenderSharedState::CollectCompletedFrames(const bool waitForAny)
    {
        if (!Device)
            return;

        const auto liveSurfaces = LiveSurfaces();
        const auto releaseFrontFrame = [this, &liveSurfaces]()
        {
            auto& frame = InFlight.front();
            const auto completionLatency =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frame.SubmittedAt).count();
            Statistics.GpuCompletionLatencyMilliseconds = completionLatency;
            if (frame.IncludesGpuVfx)
                Statistics.VfxGpuCompletionLatencyMilliseconds = completionLatency;
            for (const auto& pending : frame.GpuOcclusionReadbacks)
            {
                const auto surface = std::ranges::find(liveSurfaces, pending.SurfaceId,
                                                       [](const auto& candidate) { return candidate->Id; });
                if (surface == liveSurfaces.end())
                    continue;
                if (!CanPublishGpuOcclusionReadback(**surface, pending))
                    continue;
                auto& diagnostics = (*surface)->GpuOcclusionDiagnostics;
                auto* mapped = SDL_MapGPUTransferBuffer(Device, pending.Transfer, false);
                if (!mapped)
                {
                    const auto detail = LastSdlError();
                    if (const auto diagnostic = ClassifyDeviceFailure("SDL_MapGPUTransferBuffer", detail))
                        throw GpuDeviceLostError(*diagnostic);
                    (void)PublishGpuOcclusionReadbackValidationFailure(**surface, pending.RequestedMode);
                    (*surface)->GpuOcclusionAutomaticActive = false;
                    (*surface)->GpuOcclusionAutomaticQualifyingFrames = 0;
                    (*surface)->GpuOcclusionAutomaticCooldownFrames = 60U;
                    (*surface)->GpuOcclusionValidationCooldown = true;
                    KEIRE_CORE_WARN("GPU occlusion status readback failed for surface '{}': {}",
                                    (*surface)->Specification.Name, detail);
                    continue;
                }
                GpuOcclusionStatus status{};
                std::memcpy(&status, mapped, sizeof(status));
                SDL_UnmapGPUTransferBuffer(Device, pending.Transfer);
                const auto localLightVisible =
                    status.ConsumerVisible[static_cast<std::size_t>(GpuVisibilityConsumer::ForwardPlusLightMask)];
                const auto vfxVisible =
                    status.ConsumerVisible[static_cast<std::size_t>(GpuVisibilityConsumer::VfxVisibilityMask)];
                const auto spatialVisible =
                    status.ConsumerVisible[static_cast<std::size_t>(GpuVisibilityConsumer::SpatialVolumeMask)];
                if (status.ErrorFlags != 0U || status.Visible > pending.Candidates ||
                    localLightVisible > pending.LocalLightCandidates || vfxVisible > pending.VfxMaskEntries ||
                    spatialVisible > pending.SpatialMaskEntries)
                {
                    (void)PublishGpuOcclusionReadbackValidationFailure(**surface, pending.RequestedMode);
                    (*surface)->GpuOcclusionAutomaticActive = false;
                    (*surface)->GpuOcclusionAutomaticQualifyingFrames = 0;
                    (*surface)->GpuOcclusionAutomaticCooldownFrames = 60U;
                    (*surface)->GpuOcclusionValidationCooldown = true;
                    KEIRE_CORE_WARN("GPU occlusion status was invalid for surface '{}' (flags={}, visible={}, "
                                    "candidates={}).",
                                    (*surface)->Specification.Name, status.ErrorFlags, status.Visible,
                                    pending.Candidates);
                    continue;
                }

                diagnostics.SourceFrame = pending.SourceFrame;
                diagnostics.SourceSurfaceEpoch = pending.SourceSurfaceEpoch;
                diagnostics.SourceFrameSlot = pending.SourceFrameSlot;
                diagnostics.SourceDeviceGeneration = pending.SourceDeviceGeneration;
                const auto age = Statistics.Frame >= pending.SourceFrame ? Statistics.Frame - pending.SourceFrame : 0U;
                diagnostics.ReadbackAge = age > std::numeric_limits<std::uint32_t>::max()
                                              ? std::numeric_limits<std::uint32_t>::max()
                                              : static_cast<std::uint32_t>(age);
                diagnostics.Candidates = pending.Candidates;
                diagnostics.Visible = status.Visible;
                diagnostics.Culled = pending.Candidates - status.Visible;
                diagnostics.SafeOccluders = pending.SafeOccluders;
                diagnostics.PyramidMipCount = pending.PyramidMipCount;
                diagnostics.ReadbackValid = true;
                diagnostics.LocalLightCandidates = pending.LocalLightCandidates;
                diagnostics.LocalLightVisible = localLightVisible;
                diagnostics.LocalLightCulled = pending.LocalLightCandidates - localLightVisible;
                diagnostics.LocalLightMaskConsumed = pending.LocalLightMaskConsumed;
                diagnostics.FreshPoseSkinnedCandidates = pending.FreshPoseSkinnedCandidates;
                diagnostics.FreshPoseSkinnedDepthDraws = pending.FreshPoseSkinnedDepthDraws;
                diagnostics.VfxMaskEntries = pending.VfxMaskEntries;
                diagnostics.VfxMaskedDraws = pending.VfxMaskedDraws;
                diagnostics.VfxMaskConsumed = pending.VfxMaskConsumed;
                diagnostics.SpatialMaskEntries = pending.SpatialMaskEntries;
                diagnostics.SpatialSelectionRecords = pending.SpatialSelectionRecords;
                diagnostics.SpatialSelectionDraws = pending.SpatialSelectionDraws;
                diagnostics.SpatialMaskConsumed = pending.SpatialMaskConsumed;
                (*surface)->GpuOcclusionLatestCandidateTriangles = pending.CandidateTriangles;
                (*surface)->GpuOcclusionLatestVisibleTriangles =
                    static_cast<std::uint64_t>(status.VisibleTriangleHigh) << 32U | status.VisibleTriangleLow;

                const auto minimumUsefulCull = std::max(16U, pending.Candidates / 50U);
                if (diagnostics.RequestedMode == GpuOcclusionMode::Automatic && diagnostics.Culled < minimumUsefulCull)
                {
                    (*surface)->GpuOcclusionAutomaticActive = false;
                    (*surface)->GpuOcclusionAutomaticQualifyingFrames = 0;
                    (*surface)->GpuOcclusionAutomaticCooldownFrames = 60U;
                    (*surface)->GpuOcclusionValidationCooldown = false;
                }
            }
            std::uint64_t retiredMeshBytes = 0;
            for (const auto& retired : frame.RetiredMeshes)
                retiredMeshBytes += retired.EstimatedBytes;
            std::uint64_t retiredTextureBytes = 0;
            for (const auto& retired : frame.RetiredTextures)
                retiredTextureBytes += retired.EstimatedBytes;
            for (auto& retired : frame.Retired)
                ReleaseResources(retired);
            for (auto& retired : frame.RetiredMeshes)
                ReleaseMeshResources(retired);
            for (auto& retired : frame.RetiredSkins)
                ReleaseGpuSkinResources(retired);
            for (auto& retired : frame.RetiredTextures)
                ReleaseTextureResources(retired);
            for (auto* retired : frame.RetiredPipelines)
                SDL_ReleaseGPUGraphicsPipeline(Device, retired);
            for (auto& retired : frame.RetiredForwardPlus)
                ReleaseForwardPlusResources(retired);
            for (auto* transient : frame.TransientBuffers)
                SDL_ReleaseGPUBuffer(Device, transient);
            for (auto* transient : frame.TransientTransferBuffers)
                SDL_ReleaseGPUTransferBuffer(Device, transient);
            if (Streaming)
            {
                Streaming->ReleaseRetired(StreamingClass::Mesh, 0, retiredMeshBytes);
                Streaming->ReleaseRetired(StreamingClass::Texture, 0, retiredTextureBytes);
            }
            Statistics.FenceRetiredBytes -= std::min(Statistics.FenceRetiredBytes, frame.RetiredBytes);
            SDL_ReleaseGPUFence(Device, frame.Fence);
            if (frame.ResolvedEditorUi)
                frame.ResolvedEditorUi->ReleaseGpuTextures(Device, false);
            // Flush may observe CompleteFrame's outstanding-count transition immediately. Publish the completed
            // readback tuple before that transition returns its slot so owner-visible diagnostics cannot name a
            // workset that admission has already made reusable.
            PublishGpuOcclusionReadbackStatistics();
            CompleteFrame(frame.Frame, false);
            InFlight.erase(InFlight.begin());
        };
        const auto frontFenceReady = [this]()
        {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
            const auto injectionDepth = InjectDeviceLossAtRetirementMinimumInFlight.load(std::memory_order_acquire);
            if (injectionDepth != 0U && InFlight.size() < injectionDepth)
                return false;
            if (injectionDepth != 0U &&
                InjectDeviceLossAtRetirementMinimumInFlight.exchange(0U, std::memory_order_acq_rel) != 0U)
            {
                MarkInjectedDeviceLossForTest();
                throw GpuDeviceLostError(
                    DeviceLossDiagnostic("test fence retirement injection", "Injected GPU device loss."));
            }
#endif
            SDL_ClearError();
            if (SDL_QueryGPUFence(Device, InFlight.front().Fence))
                return true;
            if (const char* error = SDL_GetError(); error && *error)
                ThrowIfDeviceLost("SDL_QueryGPUFence", error);

            const auto now = std::chrono::steady_clock::now();
            const auto pendingFor = now - InFlight.front().SubmittedAt;
            constexpr auto healthProbeDelay = std::chrono::milliseconds(500);
            constexpr auto healthProbeInterval = std::chrono::milliseconds(250);
            constexpr auto maximumFenceRetirement = std::chrono::seconds(10);
            if (pendingFor >= maximumFenceRetirement)
            {
                throw GpuDeviceLostError(DeviceLossDiagnostic(
                    "SDL_QueryGPUFence(timeout)",
                    "GPU fence retirement timed out after 10 seconds; device responsiveness is unknown."));
            }
            if (pendingFor < healthProbeDelay || (LastFenceHealthProbeAt != std::chrono::steady_clock::time_point{} &&
                                                  now - LastFenceHealthProbeAt < healthProbeInterval))
            {
                return false;
            }

            LastFenceHealthProbeAt = now;
            SDL_ClearError();
            auto* probe = SDL_AcquireGPUCommandBuffer(Device);
            if (!probe)
            {
                const auto detail = LastSdlError();
                ThrowIfDeviceLost("SDL_AcquireGPUCommandBuffer(retirement probe)", detail);
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(retirement probe) failed: " + detail);
            }
            SDL_ClearError();
            if (!SDL_CancelGPUCommandBuffer(probe))
            {
                const auto detail = LastSdlError();
                ThrowIfDeviceLost("SDL_CancelGPUCommandBuffer(retirement probe)", detail);
                throw std::runtime_error("SDL_CancelGPUCommandBuffer(retirement probe) failed: " + detail);
            }
            return false;
        };
        while (!InFlight.empty() && frontFenceReady())
            releaseFrontFrame();
        if (InFlight.empty() || !waitForAny)
        {
            PublishGpuOcclusionReadbackStatistics();
            return;
        }

        SDL_GPUFence* fence = InFlight.front().Fence;
        const auto waitStart = std::chrono::steady_clock::now();
        if (!SDL_WaitForGPUFences(Device, true, &fence, 1))
            throw std::runtime_error("SDL_WaitForGPUFences failed: " + LastSdlError());
        Statistics.GpuFenceWaitMilliseconds +=
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - waitStart).count();

        // A successful wait is the completion contract. Retire that frame directly instead of polling recursively:
        // some drivers publish the query result a moment later, and recursive polling can exhaust the CPU stack.
        releaseFrontFrame();
        while (!InFlight.empty() && frontFenceReady())
            releaseFrontFrame();
        PublishGpuOcclusionReadbackStatistics();
    }

    void RenderSharedState::PublishGpuOcclusionReadbackStatistics()
    {
        Statistics.GpuOcclusionCandidates = 0;
        Statistics.GpuOcclusionVisible = 0;
        Statistics.GpuOcclusionCulled = 0;
        Statistics.GpuOcclusionCandidateTriangles = 0;
        Statistics.GpuOcclusionCulledTriangles = 0;
        Statistics.GpuOcclusionReadbackAge = 0;
        Statistics.GpuOcclusionReadbackValid = false;
        for (const auto& surface : LiveSurfaces())
        {
            auto& diagnostics = surface->GpuOcclusionDiagnostics;
            if (diagnostics.State == GpuOcclusionSurfaceState::Active && diagnostics.ReadbackValid)
            {
                const auto age =
                    Statistics.Frame >= diagnostics.SourceFrame ? Statistics.Frame - diagnostics.SourceFrame : 0U;
                diagnostics.ReadbackAge = age > std::numeric_limits<std::uint32_t>::max()
                                              ? std::numeric_limits<std::uint32_t>::max()
                                              : static_cast<std::uint32_t>(age);
                Statistics.GpuOcclusionCandidates += diagnostics.Candidates;
                Statistics.GpuOcclusionVisible += diagnostics.Visible;
                Statistics.GpuOcclusionCulled += diagnostics.Culled;
                Statistics.GpuOcclusionCandidateTriangles += surface->GpuOcclusionLatestCandidateTriangles;
                Statistics.GpuOcclusionCulledTriangles += surface->GpuOcclusionLatestCandidateTriangles -
                                                          std::min(surface->GpuOcclusionLatestCandidateTriangles,
                                                                   surface->GpuOcclusionLatestVisibleTriangles);
                Statistics.GpuOcclusionReadbackAge =
                    std::max(Statistics.GpuOcclusionReadbackAge, diagnostics.ReadbackAge);
                Statistics.GpuOcclusionReadbackValid = true;
            }
            surface->PublishGpuOcclusionDiagnosticsSnapshot();
        }
        if (!Statistics.GpuOcclusionReadbackValid)
            Statistics.GpuOcclusionReadbackAge = std::numeric_limits<std::uint32_t>::max();
    }

    void RenderSharedState::BeginFrame()
    {
        RequireOwner("BeginFrame");
        RethrowTerminalFailure();
        if (FrameActive)
            throw std::logic_error("A render frame is already active.");
        FrameActive = true;
        PendingSceneRequests.clear();
        PendingRuntimeUiTrees.clear();
        PendingUiSurfaceTextureBindings.clear();
        CaptureRequests.clear();
        CaptureRuntimeUiCommands.clear();
        CaptureFrameStartedAt = std::chrono::steady_clock::now();
        CaptureFrameId = NextFrameId++;
        CaptureStatistics = {};
        CaptureStatistics.Frame = CaptureFrameId;
        CaptureStatistics.AllowedFramesInFlight = Specification.MaximumFramesInFlight;
        CaptureStatistics.PlannedFrameGraphPasses = static_cast<std::uint32_t>(SceneFrameGraph.Compiled.Order.size());
        CaptureStatistics.TransientResourceAllocations =
            static_cast<std::uint32_t>(SceneFrameGraph.Compiled.TransientAllocations.size());
    }

    void RenderSharedState::PrepareFrameForExecution(const std::shared_ptr<RenderFramePacket>& frame)
    {
        Statistics = frame->CapturedStatistics;
        Statistics.RendererQueueDelayMilliseconds = frame->Timeline.QueueDelayMilliseconds;
        Statistics.FrameAdmissionWaitMilliseconds = frame->Timeline.AdmissionWaitMilliseconds;
        CollectCompletedFrames(false);
        CollectRetiredSurfaceEpochs();

        const auto vfxRetirementAge = static_cast<std::uint64_t>(Specification.MaximumFramesInFlight);
        for (auto iterator = GpuVfxWorlds.begin(); iterator != GpuVfxWorlds.end();)
        {
            if (iterator->second.LastPreparedFrame != 0U && Statistics.Frame > iterator->second.LastPreparedFrame &&
                Statistics.Frame - iterator->second.LastPreparedFrame > vfxRetirementAge)
            {
                ReleaseGpuVfxWorld(iterator->second);
                iterator = GpuVfxWorlds.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        Statistics.VfxGpuWorlds = static_cast<std::uint32_t>(GpuVfxWorlds.size());

        const auto skinRetirementAge = static_cast<std::uint64_t>(Specification.MaximumFramesInFlight);
        for (auto cacheIterator = SkinCache.begin(); cacheIterator != SkinCache.end();)
        {
            auto& entry = cacheIterator->second;
            if (entry.LastRequestedFrame != 0U && Statistics.Frame > entry.LastRequestedFrame &&
                Statistics.Frame - entry.LastRequestedFrame > skinRetirementAge)
            {
                Retire(std::move(entry.Resources));
                cacheIterator = SkinCache.erase(cacheIterator);
                continue;
            }
            for (auto instanceIterator = entry.Resources.Instances.begin();
                 instanceIterator != entry.Resources.Instances.end();)
            {
                if (instanceIterator->second.LastPreparedFrame != 0U &&
                    Statistics.Frame > instanceIterator->second.LastPreparedFrame &&
                    Statistics.Frame - instanceIterator->second.LastPreparedFrame > skinRetirementAge)
                {
                    GpuSkinResources retired;
                    retired.Instances.emplace(instanceIterator->first, std::move(instanceIterator->second));
                    Retire(std::move(retired));
                    instanceIterator = entry.Resources.Instances.erase(instanceIterator);
                }
                else
                {
                    ++instanceIterator;
                }
            }
            ++cacheIterator;
        }

        for (const auto& token : frame->Surfaces)
        {
            const auto surface = ResolveSurface(token);
            if (!surface)
                throw std::logic_error("A captured render-surface epoch expired before frame execution.");
            surface->ActiveWorksetSlot = frame->FrameSlot;
            surface->Submitted = false;
            surface->FrameClearColor = surface->Specification.ClearColor;
            EnsureSurface(*surface);
        }
        for (const auto& request : frame->Requests)
        {
            const auto surfaceLease = ResolveSurface(request.Surface);
            if (!surfaceLease)
                throw std::logic_error("A requested render-surface epoch expired before frame execution.");
            auto& surface = *surfaceLease;
            if (surface.GpuOcclusionSubmittedMode != request.Packet.Environment.GpuOcclusion)
            {
                surface.GpuOcclusionSubmittedMode = request.Packet.Environment.GpuOcclusion;
                surface.GpuOcclusionSubmissionEpoch =
                    surface.GpuOcclusionSubmissionEpoch == std::numeric_limits<std::uint64_t>::max()
                        ? 1U
                        : surface.GpuOcclusionSubmissionEpoch + 1U;
                surface.GpuOcclusionDiagnostics = {};
                surface.GpuOcclusionDiagnostics.RequestedMode = request.Packet.Environment.GpuOcclusion;
                surface.GpuOcclusionAutomaticActive = false;
                surface.GpuOcclusionAutomaticQualifyingFrames = 0;
                surface.GpuOcclusionAutomaticMinimumFrames = 0;
                surface.GpuOcclusionAutomaticCooldownFrames = 0;
                surface.GpuOcclusionValidationCooldown = false;
                surface.GpuOcclusionValidationFallbackEventPending = false;
                surface.GpuOcclusionLatestCandidateTriangles = 0;
                surface.GpuOcclusionLatestVisibleTriangles = 0;
            }
            surface.Submitted = true;
            surface.FrameClearColor = request.ClearColor;
        }
        PublishGpuOcclusionReadbackStatistics();
    }

    void RenderSharedState::CancelFrame() noexcept
    {
        FrameActive = false;
        CpuPreparation.CancelFrame();
        PendingSceneRequests.clear();
        PendingRuntimeUiTrees.clear();
        PendingUiSurfaceTextureBindings.clear();
        CaptureRequests.clear();
        CaptureRuntimeUiCommands.clear();
    }

    void RenderSharedState::QueueUiTextureRetirements(const std::span<const std::uintptr_t> logicalTextureIds)
    {
        RequireOwner("QueueUiTextureRetirements");
        if (!FrameActive)
            throw std::logic_error("UI texture retirements require an active render frame.");

        constexpr std::size_t maximumTextureRetirements = 65'536U;
        if (logicalTextureIds.size() > maximumTextureRetirements ||
            PendingUiTextureRetirements.size() > maximumTextureRetirements - logicalTextureIds.size())
        {
            throw std::length_error("Dear ImGui texture retirements exceed the asynchronous frame-packet bound.");
        }

        auto candidate = PendingUiTextureRetirements;
        candidate.insert(candidate.end(), logicalTextureIds.begin(), logicalTextureIds.end());
        std::ranges::sort(candidate);
        candidate.erase(std::unique(candidate.begin(), candidate.end()), candidate.end());
        PendingUiTextureRetirements.swap(candidate);
    }

    void RenderSharedState::Submit(SceneRenderRequest request)
    {
        RequireOwner("Submit");
        if (!FrameActive)
            throw std::logic_error("Scene render requests are accepted only during an active render frame.");
        if (!request.Scene || !request.View || !request.View->Surface())
            throw std::invalid_argument("SceneRenderRequest requires a scene, view, and render surface.");
        if (request.AdditionalScenes.size() >= SceneRenderRequest::MaximumContributions)
            throw std::invalid_argument("SceneRenderRequest exceeds the 64-scene contribution bound.");
        if (request.PrimaryContributionIndex > request.AdditionalScenes.size())
            throw std::invalid_argument("SceneRenderRequest primary contribution index is out of range.");
        ValidateRenderEnvironmentSettings(request.Environment);
        if (std::ranges::any_of(request.AdditionalScenes,
                                [](const SceneRenderContribution& contribution) { return !contribution.Scene; }))
        {
            throw std::invalid_argument("SceneRenderRequest contains an empty scene contribution.");
        }

        auto surfaceLease = std::static_pointer_cast<RenderSurfaceState>(
            RenderSystemInternalAccess::SurfaceLease(*request.View->Surface()));
        auto& surface = *surfaceLease;
        const auto owner = surface.Owner.lock();
        if (owner.get() != this)
            throw std::invalid_argument("SceneRenderRequest surface belongs to another renderer.");
        const auto surfaceToken = CaptureSurfaceToken(surfaceLease);
        if (std::ranges::any_of(
                PendingSceneRequests, [&surfaceToken](const PendingSceneRequest& pending)
                { return pending.Surface.Id == surfaceToken.Id && pending.Surface.Epoch == surfaceToken.Epoch; }))
            throw std::logic_error("A render surface may receive only one scene request per frame.");
        PendingSceneRequests.push_back({std::move(request), surfaceToken});
    }

    void RenderSharedState::CapturePendingSceneRequest(PendingSceneRequest pending, const std::uint64_t acceptedFrameId)
    {
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        SceneCaptureEnumerationCount.fetch_add(1U, std::memory_order_relaxed);
#endif
        const auto preparationStarted = std::chrono::steady_clock::now();
        auto request = std::move(pending.Request);
        const auto originalPrimary = request.PrimaryContributionIndex;
        std::vector<std::pair<std::size_t, SceneRenderContribution>> liveContributions;
        liveContributions.reserve(request.AdditionalScenes.size() + 1U);
        if (request.Scene->IsOpen())
            liveContributions.push_back({0U, {request.Scene, std::move(request.Vfx)}});
        for (std::size_t index = 0; index < request.AdditionalScenes.size(); ++index)
        {
            if (request.AdditionalScenes[index].Scene && request.AdditionalScenes[index].Scene->IsOpen())
                liveContributions.push_back({index + 1U, std::move(request.AdditionalScenes[index])});
        }
        if (liveContributions.empty())
        {
            request.DrawSceneContributions = false;
            request.AdditionalScenes.clear();
            request.PrimaryContributionIndex = 0U;
            request.Vfx = {};
        }
        else
        {
            request.Scene = liveContributions.front().second.Scene;
            request.Vfx = std::move(liveContributions.front().second.Vfx);
            request.AdditionalScenes.clear();
            request.AdditionalScenes.reserve(liveContributions.size() - 1U);
            request.PrimaryContributionIndex = 0U;
            for (std::size_t index = 0; index < liveContributions.size(); ++index)
            {
                if (liveContributions[index].first == originalPrimary)
                    request.PrimaryContributionIndex = index;
                if (index != 0U)
                    request.AdditionalScenes.push_back(std::move(liveContributions[index].second));
            }
        }
        const auto& surfaceToken = pending.Surface;
        const auto surfaceLease = ResolveSurface(surfaceToken);
        if (!surfaceLease)
            throw std::logic_error("A render-surface epoch expired before immutable frame capture.");
        const auto& surface = *surfaceLease;
        if (Specification.Mode == RenderMode::Rendered && (!surface.Specification.Depth || !DepthFormat))
            throw std::logic_error("Scene rendering requires a depth-enabled render surface.");
        const auto camera = request.View->Camera();
        if (!Math::IsFinite(camera.View) || !Math::IsFinite(camera.Projection) || !ValidColor(camera.ClearColor))
            throw std::invalid_argument("SceneRenderRequest camera contains invalid values.");
        ValidateRenderEnvironmentSettings(request.Environment);
        if (!std::isfinite(request.MaterialTimeSeconds) || request.MaterialTimeSeconds < 0.0F ||
            !std::isfinite(request.MaterialDeltaSeconds) || request.MaterialDeltaSeconds < 0.0F ||
            request.MaterialDeltaSeconds > 1.0F)
            throw std::invalid_argument("SceneRenderRequest material timing contains invalid values.");
        if (request.GlobalMaterialProperties.size() > 256U)
            throw std::invalid_argument("SceneRenderRequest exceeds the 256 global material property bound.");
        for (const auto& [name, value] : request.GlobalMaterialProperties)
        {
            if (name.empty() || name.size() > 128U || !ValidGlobalMaterialProperty(value))
                throw std::invalid_argument("SceneRenderRequest contains an invalid global material property.");
        }

        std::size_t totalCpuVfxParticles = 0;
        const auto validateContribution = [&](const Ref<Scene>& scene, const VfxRenderSnapshot& vfx)
        {
            if (!scene)
                throw std::invalid_argument("SceneRenderRequest contains an empty scene contribution.");
            if (!scene->IsOpen())
                throw std::logic_error("SceneRenderRequest cannot submit a closed scene contribution.");
            if (vfx.Particles().size() > VfxRenderSnapshot::MaximumParticles ||
                totalCpuVfxParticles > VfxRenderSnapshot::MaximumParticles - vfx.Particles().size())
            {
                throw std::invalid_argument("SceneRenderRequest exceeds the aggregate VFX particle packet bound.");
            }
            totalCpuVfxParticles += vfx.Particles().size();
            for (const auto& particle : vfx.Particles())
            {
                if (!Math::IsFinite(particle.Position) || !Math::IsFinite(particle.PreviousPosition) ||
                    !Math::IsFinite(particle.Rotation) || !Math::IsFinite(particle.Tint) ||
                    !std::isfinite(particle.Size) || particle.Size < 0.0F ||
                    particle.Renderer > VfxRendererType::Volumetric)
                {
                    throw std::invalid_argument("SceneRenderRequest contains an invalid VFX particle.");
                }
            }
            if (!vfx.GpuEmitters().empty() &&
                (vfx.WorldId() == 0 || vfx.ParticleCapacity() == 0 || vfx.ParticleCapacity() > 10'000'000U ||
                 !std::isfinite(vfx.DeltaSeconds()) || vfx.DeltaSeconds() < 0.0F))
            {
                throw std::invalid_argument("SceneRenderRequest contains an invalid GPU VFX snapshot.");
            }
            for (const auto& emitter : vfx.GpuEmitters())
            {
                if (!emitter.Handle || emitter.Revision == 0 || !Math::IsFinite(emitter.Position) ||
                    !Math::IsFinite(emitter.Rotation) || !Math::IsFinite(emitter.ShapeExtent) ||
                    !Math::IsFinite(emitter.VelocityMinimum) || !Math::IsFinite(emitter.VelocityMaximum) ||
                    !Math::IsFinite(emitter.Acceleration) || !Math::IsFinite(emitter.ColorStart) ||
                    !Math::IsFinite(emitter.ColorEnd) || !std::isfinite(emitter.LifetimeMinimum) ||
                    !std::isfinite(emitter.LifetimeMaximum) || emitter.LifetimeMinimum <= 0.0F ||
                    emitter.LifetimeMaximum < emitter.LifetimeMinimum || !std::isfinite(emitter.SizeStart) ||
                    !std::isfinite(emitter.SizeEnd) || !std::isfinite(emitter.SimulationDeltaSeconds) ||
                    emitter.SimulationDeltaSeconds < 0.0F || emitter.SimulationDeltaSeconds > 80.0F ||
                    emitter.Renderer > VfxRendererType::Volumetric ||
                    emitter.DataType > VfxParticleDataType::ParticleStrip || emitter.ParticlesPerStrip == 0 ||
                    (emitter.Renderer == VfxRendererType::Ribbon &&
                     emitter.DataType != VfxParticleDataType::ParticleStrip) ||
                    emitter.Capacity == 0 || emitter.Capacity > vfx.ParticleCapacity() ||
                    (emitter.Renderer == VfxRendererType::Mesh && !emitter.Mesh))
                {
                    throw std::invalid_argument("SceneRenderRequest contains an invalid GPU VFX emitter.");
                }
            }
        };
        if (request.DrawSceneContributions)
        {
            validateContribution(request.Scene, request.Vfx);
            for (const auto& contribution : request.AdditionalScenes)
                validateContribution(contribution.Scene, contribution.Vfx);
        }

        const auto& primaryContribution = request.PrimaryContributionIndex == 0
                                              ? request.Scene
                                              : request.AdditionalScenes[request.PrimaryContributionIndex - 1U].Scene;
        SceneRenderPacket packet;
        packet.Scene = primaryContribution->Asset();
        packet.Camera = camera;
        packet.Environment = request.Environment;
        packet.GlobalMaterialProperties = std::move(request.GlobalMaterialProperties);
        if (request.DrawSceneContributions)
        {
            packet.Lighting = ResolveLighting(primaryContribution);
            if (!packet.Lighting.Enabled)
            {
                packet.Lighting = ResolveLighting(request.Scene);
                for (const auto& contribution : request.AdditionalScenes)
                {
                    if (packet.Lighting.Enabled)
                        break;
                    packet.Lighting = ResolveLighting(contribution.Scene);
                }
            }
            packet.BakedLighting = primaryContribution->BakedLighting();
        }
#if defined(KEIRE_ENABLE_TEST_HOOKS)
        LastCapturedDirectionalLightEntity.store(packet.Lighting.Entity.Value().Low(), std::memory_order_relaxed);
#endif
        packet.DrawGrid = request.DrawGrid;
        packet.MaterialTimeSeconds = request.MaterialTimeSeconds;
        packet.MaterialDeltaSeconds = request.MaterialDeltaSeconds;
        packet.AcceptedFrameId = acceptedFrameId;
        packet.FrameIndex = request.FrameIndex;
        packet.VfxSnapshots.reserve(request.AdditionalScenes.size() + 1U);

        const auto appendContribution = [&](const Ref<Scene>& scene, VfxRenderSnapshot vfx)
        {
            const auto contributionOrder = static_cast<std::uint32_t>(packet.VfxSnapshots.size());
            auto localLights = ResolveLocalLights(scene);
            for (auto& light : localLights)
                light.ContributionOrder = contributionOrder;
            packet.LocalLights.insert(packet.LocalLights.end(), std::make_move_iterator(localLights.begin()),
                                      std::make_move_iterator(localLights.end()));
            auto reflectionProbes = ResolveReflectionProbes(scene);
            auto lightProbeVolumes = ResolveLightProbeVolumes(scene);
            packet.SpatialContributions.push_back(
                {scene->Asset(), scene->BakedLighting(), reflectionProbes, lightProbeVolumes});
            packet.ReflectionProbes.insert(packet.ReflectionProbes.end(),
                                           std::make_move_iterator(reflectionProbes.begin()),
                                           std::make_move_iterator(reflectionProbes.end()));
            packet.LightProbeVolumes.insert(packet.LightProbeVolumes.end(),
                                            std::make_move_iterator(lightProbeVolumes.begin()),
                                            std::make_move_iterator(lightProbeVolumes.end()));

            const auto sceneAsset = scene->Asset();
            const auto renderEntities = scene->Query<MeshRendererComponent>();
            for (const auto& entity : renderEntities)
            {
                if (!entity.ActiveInHierarchy())
                    continue;
                const auto renderer = entity.GetComponent<MeshRendererComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (!renderer || !renderer->Enabled() || !renderer->Visible() || !transform)
                    continue;
                std::vector<Matrix4> skinPalette;
                AssetId skin;
                AssetId skinSkeleton;
                std::uint64_t poseGeneration = 0;
                if (const auto animator = entity.GetComponent<AnimatorComponent>(); animator && animator->Enabled())
                {
                    skinPalette.assign(animator->SkinPalette().begin(), animator->SkinPalette().end());
                    skin = animator->SkinnedMesh();
                    skinSkeleton = animator->Skeleton();
                    poseGeneration = animator->PoseGeneration();
                }
                packet.DrawItems.push_back({renderer->Mesh(),
                                            {renderer->Materials().begin(), renderer->Materials().end()},
                                            renderer->MaterialProperties(),
                                            {},
                                            transform->PresentationWorldMatrix(),
                                            renderer->Tint(),
                                            entity.Id(),
                                            skin,
                                            skinSkeleton,
                                            std::move(skinPalette),
                                            renderer->CastShadows(),
                                            renderer->ReceiveShadows(),
                                            renderer->AlwaysVisible()});
                auto& item = packet.DrawItems.back();
                item.MaterialInstanceProperties = renderer->AllMaterialInstanceProperties();
                item.Scene = sceneAsset;
                item.ContributionOrder = contributionOrder;
                item.VisibilityClass = GpuVisibilityClassForDraw(static_cast<bool>(item.Skin), false);
                item.PoseGeneration = poseGeneration;
            }
            for (const auto& particle : vfx.Particles())
            {
                if (particle.Renderer == VfxRendererType::Sprite)
                    ++CaptureStatistics.VfxSpriteParticles;
                else if (particle.Renderer == VfxRendererType::Mesh)
                    ++CaptureStatistics.VfxMeshParticles;
                else if (particle.Renderer == VfxRendererType::Ribbon)
                    ++CaptureStatistics.VfxRibbonParticles;
                else
                    ++CaptureStatistics.VfxVolumetricParticles;
                if (auto item = VfxMeshDrawItem(particle))
                {
                    item->Scene = sceneAsset;
                    item->ContributionOrder = contributionOrder;
                    packet.DrawItems.push_back(std::move(*item));
                }
            }
            CaptureStatistics.DroppedVfxParticles += vfx.DroppedParticles();
            packet.VfxSnapshots.push_back(std::move(vfx));
        };
        if (request.DrawSceneContributions)
        {
            appendContribution(request.Scene, std::move(request.Vfx));
            for (auto& contribution : request.AdditionalScenes)
                appendContribution(contribution.Scene, std::move(contribution.Vfx));
        }

#if defined(KEIRE_ENABLE_TEST_HOOKS)
        {
            std::scoped_lock lock(PublicationMutex);
            LastCapturedPrimaryScene = packet.Scene;
            LastCapturedPrimaryBakedLighting = packet.BakedLighting;
            LastCapturedCamera = packet.Camera;
            LastCapturedEnvironment = packet.Environment;
            LastCapturedClearColor = camera.ClearColor;
            LastCapturedDrawContributionOrder.clear();
            LastCapturedDrawEntities.clear();
            LastCapturedDrawContributionOrder.reserve(packet.DrawItems.size());
            LastCapturedDrawEntities.reserve(packet.DrawItems.size());
            for (const auto& item : packet.DrawItems)
            {
                LastCapturedDrawContributionOrder.push_back(item.ContributionOrder);
                LastCapturedDrawEntities.push_back(item.Entity);
            }
            LastCapturedSpatialScenes.clear();
            LastCapturedSpatialBakedLighting.clear();
            LastCapturedSpatialScenes.reserve(packet.SpatialContributions.size());
            LastCapturedSpatialBakedLighting.reserve(packet.SpatialContributions.size());
            for (const auto& contribution : packet.SpatialContributions)
            {
                LastCapturedSpatialScenes.push_back(contribution.Scene);
                LastCapturedSpatialBakedLighting.push_back(contribution.BakedLighting);
            }
            LastCapturedLocalLights = packet.LocalLights.size();
            LastCapturedReflectionProbes = packet.ReflectionProbes.size();
            LastCapturedLightProbeVolumes = packet.LightProbeVolumes.size();
        }
#endif

        const auto lightFrustum = GeometryDetail::BuildFrustumPlanes(Math::Multiply(camera.Projection, camera.View));
        CaptureStatistics.CulledLocalLights += static_cast<std::uint32_t>(
            std::erase_if(packet.LocalLights,
                          [&](const SceneLocalLight& light)
                          {
                              const auto radius = std::max(light.Range, 0.0F);
                              const MeshBounds bounds{
                                  {light.Position.X - radius, light.Position.Y - radius, light.Position.Z - radius},
                                  {light.Position.X + radius, light.Position.Y + radius, light.Position.Z + radius}};
                              return !GeometryDetail::IntersectsFrustum(lightFrustum, bounds);
                          }));
        CaptureRequests.push_back({std::move(packet), surfaceToken, camera.ClearColor});
        CpuPreparation.Accumulate(
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - preparationStarted).count());
    }

    const GpuMeshResources& RenderSharedState::ResolveMesh(const AssetId id)
    {
        if (!id || id == MeshAsset::CubeId())
            return DefaultMesh;
        if (id == MeshAsset::ErrorId() || !Assets)
        {
            if (id == MeshAsset::ErrorId() || !id)
                return ErrorMesh;
            if (!MeshAsset::IsBuiltin(id))
                return ErrorMesh;
        }

        auto [iterator, inserted] = MeshCache.try_emplace(id);
        auto& entry = iterator->second;
        if (inserted && MeshAsset::IsBuiltin(id))
        {
            if (const auto mesh = MeshAsset::ResolveBuiltin(id))
            {
                entry.Resources = CreateMeshResources(*mesh);
                entry.Resources.Revision = 1;
                entry.LoadedRevision = 1;
                entry.LastAttemptedRevision = 1;
            }
        }
        else if (inserted)
            entry.Handle = Assets->Load<MeshAsset>(id, AssetPriority::High);
        if (MeshAsset::IsBuiltin(id))
            return entry.Resources.Empty() ? ErrorMesh : entry.Resources;
        const auto revision = entry.Handle.Revision();
        if (revision != 0 && revision > entry.LastAttemptedRevision)
        {
            entry.LastAttemptedRevision = revision;
            if (const auto mesh = entry.Handle.TryGetLoaded())
            {
                try
                {
                    auto replacement = CreateMeshResources(*mesh);
                    replacement.Revision = revision;
                    Retire(std::exchange(entry.Resources, replacement));
                    entry.LoadedRevision = revision;
                }
                catch (const std::exception& error)
                {
                    ThrowIfDeviceLost("mesh GPU rebuild", error.what());
                    KEIRE_CORE_ERROR("Mesh GPU rebuild failed for id={} revision={}: {}", id.ToString(), revision,
                                     error.what());
                }
            }
        }
        return entry.Resources.Empty() ? ErrorMesh : entry.Resources;
    }

    const GpuTextureResources& RenderSharedState::ResolveTexture(const AssetId id)
    {
        if (!id || !Assets)
            return CheckerboardTexture;
        auto [iterator, inserted] = TextureCache.try_emplace(id);
        auto& entry = iterator->second;
        if (inserted)
            entry.Handle = Assets->Load<Texture2DAsset>(id, AssetPriority::High);
        const auto revision = entry.Handle.Revision();
        if (revision != 0 && revision > entry.LastAttemptedRevision)
        {
            entry.LastAttemptedRevision = revision;
            if (const auto texture = entry.Handle.TryGetLoaded())
            {
                try
                {
                    auto replacement = CreateTextureResources(*texture);
                    Retire(std::exchange(entry.Resources, replacement));
                    entry.LoadedRevision = revision;
                }
                catch (const std::exception& error)
                {
                    ThrowIfDeviceLost("texture GPU rebuild", error.what());
                    KEIRE_CORE_ERROR("Texture GPU rebuild failed for id={} revision={}: {}", id.ToString(), revision,
                                     error.what());
                }
            }
        }
        return entry.Resources.Empty() ? CheckerboardTexture : entry.Resources;
    }

    const GpuTextureResources& RenderSharedState::ResolveCookieAtlas(const std::span<const AssetId> cookies)
    {
        constexpr std::uint32_t slotResolution = 128;
        constexpr std::uint32_t columns = 4;
        constexpr std::uint32_t rows = 2;
        bool anyCookie = false;
        bool changed = CookieAtlas.Empty();
        for (std::size_t slot = 0; slot < CookieAtlasAssets.size(); ++slot)
        {
            const auto id = slot < cookies.size() ? cookies[slot] : AssetId{};
            anyCookie |= static_cast<bool>(id);
            if (CookieAtlasAssets[slot] != id)
            {
                CookieAtlasAssets[slot] = id;
                CookieAtlasHandles[slot] = id && Assets ? Assets->Load<Texture2DAsset>(id, AssetPriority::High)
                                                        : AssetHandle<Texture2DAsset>{};
                CookieAtlasRevisions[slot] = 0;
                changed = true;
            }
            const auto revision = CookieAtlasHandles[slot] ? CookieAtlasHandles[slot].Revision() : 0U;
            if (CookieAtlasRevisions[slot] != revision)
            {
                CookieAtlasRevisions[slot] = revision;
                changed = true;
            }
        }
        if (!anyCookie)
            return WhiteTexture;
        if (!changed)
            return CookieAtlas.Empty() ? WhiteTexture : CookieAtlas;

        TextureMipLevel atlas;
        atlas.Width = slotResolution * columns;
        atlas.Height = slotResolution * rows;
        atlas.Pixels.assign(static_cast<std::size_t>(atlas.Width) * atlas.Height * 4U, std::byte{255});
        for (std::size_t slot = 0; slot < CookieAtlasHandles.size(); ++slot)
        {
            const auto source = CookieAtlasHandles[slot].TryGetLoaded();
            if (!source || source->Mips().empty())
                continue;
            const auto& sourceMip = source->Mips().front();
            if (sourceMip.Width == 0U || sourceMip.Height == 0U ||
                sourceMip.Pixels.size() != static_cast<std::size_t>(sourceMip.Width) * sourceMip.Height * 4U)
                continue;
            const auto originX = static_cast<std::uint32_t>(slot % columns) * slotResolution;
            const auto originY = static_cast<std::uint32_t>(slot / columns) * slotResolution;
            for (std::uint32_t y = 0; y < slotResolution; ++y)
            {
                const auto sourceY = std::min(y * sourceMip.Height / slotResolution, sourceMip.Height - 1U);
                for (std::uint32_t x = 0; x < slotResolution; ++x)
                {
                    const auto sourceX = std::min(x * sourceMip.Width / slotResolution, sourceMip.Width - 1U);
                    const auto sourceOffset = (static_cast<std::size_t>(sourceY) * sourceMip.Width + sourceX) * 4U;
                    const auto destinationOffset =
                        (static_cast<std::size_t>(originY + y) * atlas.Width + originX + x) * 4U;
                    std::memcpy(atlas.Pixels.data() + destinationOffset, sourceMip.Pixels.data() + sourceOffset, 4U);
                }
            }
        }
        TextureImportSettings settings;
        settings.Semantic = TextureSemantic::Color;
        settings.ColorSpace = TextureColorSpace::Srgb;
        settings.Mips = TextureMipPolicy::None;
        settings.Sampler.AddressU = TextureAddressMode::Clamp;
        settings.Sampler.AddressV = TextureAddressMode::Clamp;
        auto replacement = CreateTextureResources(
            *CreateRef<Texture2DAsset>(settings, std::vector<TextureMipLevel>{std::move(atlas)}));
        Retire(std::exchange(CookieAtlas, replacement));
        return CookieAtlas;
    }

    const GpuTextureResources& RenderSharedState::ResolveLightingTexture(const AssetId id, const bool cubeArray,
                                                                         const bool whiteFallback)
    {
        const auto& fallback = cubeArray       ? DefaultReflectionCubeArray
                               : whiteFallback ? DefaultLightingMaskArray
                                               : DefaultLightingArray;
        if (!id || !Assets)
            return fallback;
        auto [iterator, inserted] = LightingTextureCache.try_emplace(id);
        auto& entry = iterator->second;
        if (inserted)
            entry.Handle = Assets->Load<LightingTextureArrayAsset>(id, AssetPriority::High);
        const auto revision = entry.Handle.Revision();
        if (revision != 0 && revision > entry.LastAttemptedRevision)
        {
            entry.LastAttemptedRevision = revision;
            if (const auto texture = entry.Handle.TryGetLoaded())
            {
                try
                {
                    const auto expected =
                        cubeArray ? LightingTextureTarget::CubeArray : LightingTextureTarget::Texture2DArray;
                    if (texture->Definition().Target != expected)
                        throw std::invalid_argument("Baked-lighting texture target does not match its binding.");
                    auto replacement = CreateLightingTextureResources(*texture);
                    Retire(std::exchange(entry.Resources, replacement));
                    entry.LoadedRevision = revision;
                }
                catch (const std::exception& error)
                {
                    ThrowIfDeviceLost("baked-lighting GPU rebuild", error.what());
                    KEIRE_CORE_ERROR("Baked-lighting GPU rebuild failed for id={} revision={}: {}", id.ToString(),
                                     revision, error.what());
                }
            }
        }
        return entry.Resources.Empty() ? fallback : entry.Resources;
    }

    Ref<const LightingSetAsset> RenderSharedState::ResolveLightingSet(const AssetId id)
    {
        if (!id || !Assets)
            return {};
        auto [iterator, inserted] = LightingSetCache.try_emplace(id);
        if (inserted)
            iterator->second = Assets->Load<LightingSetAsset>(id, AssetPriority::High);
        return iterator->second.TryGetLoaded();
    }

    Ref<const LightProbeVolumeAsset> RenderSharedState::ResolveLightProbeVolume(const AssetId id)
    {
        if (!id || !Assets)
            return {};
        auto [iterator, inserted] = LightProbeVolumeCache.try_emplace(id);
        if (inserted)
            iterator->second = Assets->Load<LightProbeVolumeAsset>(id, AssetPriority::High);
        return iterator->second.TryGetLoaded();
    }

    const GpuTextureResources& RenderSharedState::DefaultTexture(const ShaderTextureSemantic semantic) const noexcept
    {
        switch (semantic)
        {
        case ShaderTextureSemantic::BaseColor:
            return WhiteTexture;
        case ShaderTextureSemantic::Normal:
            return FlatNormalTexture;
        case ShaderTextureSemantic::MetallicRoughness:
        case ShaderTextureSemantic::Occlusion:
            return NeutralOrmTexture;
        case ShaderTextureSemantic::Emissive:
            return BlackTexture;
        case ShaderTextureSemantic::Metallic:
            return BlackDataTexture;
        case ShaderTextureSemantic::Roughness:
            return WhiteDataTexture;
        case ShaderTextureSemantic::Generic:
        default:
            return CheckerboardTexture;
        }
    }

    Ref<const MaterialAsset> RenderSharedState::ResolveMaterial(const AssetId id)
    {
        if (!id || !Assets)
            return {};
        auto [iterator, inserted] = MaterialCache.try_emplace(id);
        auto& entry = iterator->second;
        if (inserted)
            entry.Handle = Assets->Load<MaterialAsset>(id, AssetPriority::High);
        const auto revision = entry.Handle.Revision();
        if (revision != 0 && revision > entry.LastAttemptedRevision)
        {
            entry.LastAttemptedRevision = revision;
            if (const auto material = entry.Handle.TryGetLoaded())
            {
                entry.LastGood = material;
                entry.LoadedRevision = revision;
            }
        }
        return entry.LastGood;
    }

    GpuShaderEntry* RenderSharedState::ResolveShader(const AssetId id, const SDL_GPUSampleCount samples,
                                                     MaterialSurfaceState surface, const bool explicitSurface)
    {
        if (!id || !Assets)
            return nullptr;
        auto [iterator, inserted] = ShaderCache.try_emplace(id);
        auto& entry = iterator->second;
        if (inserted)
            entry.Handle = Assets->Load<ShaderAsset>(id, AssetPriority::High);
        const auto revision = entry.Handle.Revision();
        if (revision != 0 && revision > entry.LastAttemptedRevision)
        {
            entry.LastAttemptedRevision = revision;
            if (const auto shader = entry.Handle.TryGetLoaded())
            {
                try
                {
                    if (!explicitSurface && shader->Definition().Blend)
                        surface.AlphaMode = MaterialAlphaMode::Blend;
                    auto replacement = CreateAssetPipeline(shader->Definition(), samples, surface);
                    for (const auto& pipeline : entry.Pipelines)
                        Retire(pipeline.Handle);
                    entry.Pipelines = {{samples, surface.AlphaMode, surface.DoubleSided, replacement}};
                    entry.LastGood = shader;
                    entry.LoadedRevision = revision;
                }
                catch (const std::exception& error)
                {
                    ThrowIfDeviceLost("shader GPU rebuild", error.what());
                    KEIRE_CORE_ERROR("Shader GPU rebuild failed for id={} revision={}: {}", id.ToString(), revision,
                                     error.what());
                }
            }
        }
        if (!entry.LastGood)
            return nullptr;
        if (!explicitSurface && entry.LastGood->Definition().Blend)
            surface.AlphaMode = MaterialAlphaMode::Blend;
        auto pipeline = std::ranges::find_if(entry.Pipelines,
                                             [&](const GpuShaderEntry::Pipeline& candidate)
                                             {
                                                 return candidate.Samples == samples &&
                                                        candidate.AlphaMode == surface.AlphaMode &&
                                                        candidate.DoubleSided == surface.DoubleSided;
                                             });
        if (pipeline == entry.Pipelines.end())
        {
            try
            {
                entry.Pipelines.push_back({samples, surface.AlphaMode, surface.DoubleSided,
                                           CreateAssetPipeline(entry.LastGood->Definition(), samples, surface)});
            }
            catch (const std::exception& error)
            {
                ThrowIfDeviceLost("shader pipeline creation", error.what());
                KEIRE_CORE_ERROR("Shader pipeline creation failed for id={}: {}", id.ToString(), error.what());
                return nullptr;
            }
        }
        return &entry;
    }

    const ResolvedAssetMaterial* RenderSharedState::ResolveAssetMaterial(const AssetId id,
                                                                         const SDL_GPUSampleCount samples)
    {
        if (const auto cached = MaterialCache.find(id); cached != MaterialCache.end())
        {
            const auto binding = std::ranges::find(cached->second.Bindings, samples, &GpuMaterialBindingEntry::Samples);
            if (binding != cached->second.Bindings.end() && binding->LastDependencyCheckFrame == Statistics.Frame)
                return binding->Binding.Pipeline ? &binding->Binding : nullptr;
        }

        const auto material = ResolveMaterial(id);
        auto materialEntry = MaterialCache.find(id);
        if (materialEntry == MaterialCache.end())
            return nullptr;
        auto& cache = materialEntry->second;
        auto binding = std::ranges::find(cache.Bindings, samples, &GpuMaterialBindingEntry::Samples);
        if (binding == cache.Bindings.end())
        {
            cache.Bindings.emplace_back();
            binding = std::prev(cache.Bindings.end());
            binding->Samples = samples;
        }
        ++MaterialDependencyChecks;
        const auto finishDependencyCheck = [&](const ResolvedAssetMaterial* result) noexcept
        {
            binding->LastDependencyCheckFrame = Statistics.Frame;
            return result;
        };
        std::uint64_t stamp = 1469598103934665603ULL;
        stamp = HashDependencyStamp(stamp, cache.LastAttemptedRevision);
        stamp = HashDependencyStamp(stamp, cache.LoadedRevision);
        stamp = HashDependencyStamp(stamp, static_cast<std::uint64_t>(samples));
        stamp = HashDependencyStamp(stamp, static_cast<std::uint64_t>(ColorFormat));
        stamp = HashDependencyStamp(stamp, static_cast<std::uint64_t>(DepthFormat));
        stamp =
            HashDependencyStamp(stamp, static_cast<std::uint64_t>(material ? material->Definition().Surface.AlphaMode
                                                                           : MaterialAlphaMode::Opaque));
        stamp = HashDependencyStamp(
            stamp, material ? std::bit_cast<std::uint32_t>(material->Definition().Surface.AlphaCutoff) : 0U);
        stamp = HashDependencyStamp(stamp, material && material->Definition().Surface.DoubleSided ? 1U : 0U);
        bool failedDependencyRevision = cache.LoadedRevision != 0 && cache.LastAttemptedRevision > cache.LoadedRevision;
        if (!material || !material->Definition().Shader)
        {
            binding->LastAttemptedDependencyStamp = stamp;
            return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);
        }
        stamp = HashDependencyStamp(stamp, material->Definition().Shader);
        auto* shader = ResolveShader(material->Definition().Shader, samples, material->Definition().Surface,
                                     material->Definition().SchemaVersion >= 2);
        if (!shader)
        {
            binding->LastAttemptedDependencyStamp = stamp;
            return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);
        }
        stamp = HashDependencyStamp(stamp, shader->LastAttemptedRevision);
        stamp = HashDependencyStamp(stamp, shader->LoadedRevision);
        auto surface = material->Definition().Surface;
        if (material->Definition().SchemaVersion < 2 && shader->LastGood->Definition().Blend)
            surface.AlphaMode = MaterialAlphaMode::Blend;
        const auto pipeline = std::ranges::find_if(shader->Pipelines,
                                                   [&](const GpuShaderEntry::Pipeline& candidate)
                                                   {
                                                       return candidate.Samples == samples &&
                                                              candidate.AlphaMode == surface.AlphaMode &&
                                                              candidate.DoubleSided == surface.DoubleSided;
                                                   });
        if (pipeline == shader->Pipelines.end())
        {
            binding->LastAttemptedDependencyStamp = stamp;
            return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);
        }

        const auto& properties = material->Definition().Properties;
        for (const auto& [name, value] : properties)
        {
            (void)value;
            if (std::ranges::find(shader->LastGood->Definition().Properties, name, &ShaderPropertyDefinition::Name) ==
                shader->LastGood->Definition().Properties.end())
            {
                binding->LastAttemptedDependencyStamp = stamp;
                return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);
            }
        }

        failedDependencyRevision |=
            shader->LoadedRevision != 0 && shader->LastAttemptedRevision > shader->LoadedRevision;
        for (const auto& property : shader->LastGood->Definition().Properties)
        {
            if (property.Type != ShaderPropertyType::Texture2D)
                continue;
            const auto found = properties.find(property.Name);
            AssetId texture = property.DefaultTexture;
            if (found != properties.end())
            {
                const auto* selected = std::get_if<AssetId>(&found->second);
                if (!selected)
                {
                    binding->LastAttemptedDependencyStamp = stamp;
                    return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);
                }
                texture = *selected;
            }
            stamp = HashDependencyStamp(stamp, texture);
            if (!texture)
            {
                stamp = HashDependencyStamp(stamp, static_cast<std::uint64_t>(property.TextureSemantic));
                continue;
            }
            (void)ResolveTexture(texture);
            const auto textureEntry = TextureCache.find(texture);
            if (textureEntry == TextureCache.end())
                continue;
            stamp = HashDependencyStamp(stamp, textureEntry->second.LastAttemptedRevision);
            stamp = HashDependencyStamp(stamp, textureEntry->second.LoadedRevision);
            failedDependencyRevision |=
                textureEntry->second.LoadedRevision != 0 &&
                textureEntry->second.LastAttemptedRevision > textureEntry->second.LoadedRevision;
        }
        if (stamp == binding->LastAttemptedDependencyStamp)
            return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);
        binding->LastAttemptedDependencyStamp = stamp;
        if (failedDependencyRevision)
            return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);

        try
        {
            ResolvedAssetMaterial result;
            result.Pipeline = pipeline->Handle;
            result.Surface = surface;
            const auto& definition = shader->LastGood->Definition();
            result.Topology = definition.Topology;
            result.Culling = definition.Culling;
            result.OcclusionSupport = definition.OcclusionSupport;
            result.MaximumWorldPositionDisplacementRadius = definition.MaximumWorldPositionDisplacementRadius;
            result.ReceivesShadows = definition.ReceivesShadows;
            result.DepthTest = definition.DepthTest;
            result.DepthWrite = definition.DepthWrite;
            result.UsesForwardPlus = definition.UsesForwardPlus;
            result.UsesInstancing = definition.UsesInstancing;
            result.UsesImageBasedLighting = definition.UsesImageBasedLighting;
            result.UsesSpatialLighting = definition.SpatialLightingAbiVersion >= 2U;
            result.UsesVertexMaterialParameters = definition.UsesVertexMaterialParameters;
            result.SpatialLightingAbiVersion = definition.SpatialLightingAbiVersion;
            result.InstanceAddressingAbiVersion = definition.InstanceAddressingAbiVersion;
            for (const auto& property : shader->LastGood->Definition().Properties)
            {
                const auto found = properties.find(property.Name);
                if (property.Type == ShaderPropertyType::Texture2D)
                {
                    result.Properties.emplace(
                        property.Name, ResolvedAssetMaterial::PropertyBinding{property.Type, property.TextureSemantic,
                                                                              result.Textures.size()});
                    AssetId texture = property.DefaultTexture;
                    if (found != properties.end())
                    {
                        const auto* selected = std::get_if<AssetId>(&found->second);
                        if (!selected)
                            throw std::runtime_error("Material texture property has an invalid value type.");
                        texture = *selected;
                    }
                    const auto& resolved = texture ? ResolveTexture(texture) : DefaultTexture(property.TextureSemantic);
                    if (result.Textures.size() >= 16)
                        throw std::runtime_error("Material exceeds the fragment texture binding limit.");
                    result.Textures.push_back({resolved.Texture, resolved.Sampler});
                    continue;
                }

                Vector4 packed = property.DefaultValue;
                if (found != properties.end())
                {
                    const auto& value = found->second;
                    if (const auto* scalar = std::get_if<float>(&value))
                        packed = {*scalar, 0.0F, 0.0F, 0.0F};
                    else if (const auto* vector2 = std::get_if<Vector2>(&value))
                        packed = {vector2->X, vector2->Y, 0.0F, 0.0F};
                    else if (const auto* vector3 = std::get_if<Vector3>(&value))
                        packed = {vector3->X, vector3->Y, vector3->Z, 0.0F};
                    else if (const auto* vector4 = std::get_if<Vector4>(&value))
                        packed = *vector4;
                    else if (const auto* color = std::get_if<Color>(&value))
                        packed = {color->Red, color->Green, color->Blue, color->Alpha};
                    else
                        throw std::runtime_error("Material numeric property has an invalid value type.");
                }
                if (property.Name == "Tint")
                    result.TintSlot = result.NumericProperties.size();
                if (result.NumericProperties.size() >= 64)
                    throw std::runtime_error("Material exceeds the numeric property binding limit.");
                result.Properties.emplace(
                    property.Name, ResolvedAssetMaterial::PropertyBinding{property.Type, property.TextureSemantic,
                                                                          result.NumericProperties.size()});
                result.NumericProperties.push_back(packed);
            }
            if (result.NumericProperties.empty())
                result.NumericProperties.emplace_back();
            binding->Binding = std::move(result);
            binding->LastGoodDependencyStamp = stamp;
            ++MaterialBindingBuilds;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("material GPU binding rebuild", error.what());
            KEIRE_CORE_ERROR("Material GPU binding rebuild failed for id={} revision={}: {}", id.ToString(),
                             cache.LoadedRevision, error.what());
        }
        return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);
    }
} // namespace Keire::RenderBackend
