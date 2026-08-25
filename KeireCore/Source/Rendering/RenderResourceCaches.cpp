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

    void RenderSharedState::CollectCompletedFrames()
    {
        if (!Device)
            return;

        const auto liveSurfaces = LiveSurfaces();
        const auto releaseFrontFrame = [this, &liveSurfaces]()
        {
            auto frame = std::move(InFlight.front());
            InFlight.pop_front();
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
                    (void)PublishGpuOcclusionReadbackValidationFailure(**surface, pending.RequestedMode);
                    (*surface)->GpuOcclusionAutomaticActive = false;
                    (*surface)->GpuOcclusionAutomaticQualifyingFrames = 0;
                    (*surface)->GpuOcclusionAutomaticCooldownFrames = 60U;
                    (*surface)->GpuOcclusionValidationCooldown = true;
                    KEIRE_CORE_WARN("GPU occlusion status readback failed for surface '{}': {}",
                                    (*surface)->Specification.Name, LastSdlError());
                    continue;
                }
                GpuOcclusionStatus status{};
                std::memcpy(&status, mapped, sizeof(status));
                SDL_UnmapGPUTransferBuffer(Device, pending.Transfer);
                if (status.ErrorFlags != 0U || status.Visible > pending.Candidates)
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
        };
        while (!InFlight.empty() && SDL_QueryGPUFence(Device, InFlight.front().Fence))
            releaseFrontFrame();
        if (InFlight.size() < Specification.MaximumFramesInFlight)
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
        while (!InFlight.empty() && SDL_QueryGPUFence(Device, InFlight.front().Fence))
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
            if (diagnostics.State != GpuOcclusionSurfaceState::Active || !diagnostics.ReadbackValid)
                continue;
            const auto age =
                Statistics.Frame >= diagnostics.SourceFrame ? Statistics.Frame - diagnostics.SourceFrame : 0U;
            diagnostics.ReadbackAge = age > std::numeric_limits<std::uint32_t>::max()
                                          ? std::numeric_limits<std::uint32_t>::max()
                                          : static_cast<std::uint32_t>(age);
            Statistics.GpuOcclusionCandidates += diagnostics.Candidates;
            Statistics.GpuOcclusionVisible += diagnostics.Visible;
            Statistics.GpuOcclusionCulled += diagnostics.Culled;
            Statistics.GpuOcclusionCandidateTriangles += surface->GpuOcclusionLatestCandidateTriangles;
            Statistics.GpuOcclusionCulledTriangles +=
                surface->GpuOcclusionLatestCandidateTriangles -
                std::min(surface->GpuOcclusionLatestCandidateTriangles, surface->GpuOcclusionLatestVisibleTriangles);
            Statistics.GpuOcclusionReadbackAge = std::max(Statistics.GpuOcclusionReadbackAge, diagnostics.ReadbackAge);
            Statistics.GpuOcclusionReadbackValid = true;
        }
        if (!Statistics.GpuOcclusionReadbackValid)
            Statistics.GpuOcclusionReadbackAge = std::numeric_limits<std::uint32_t>::max();
    }

    void RenderSharedState::BeginFrame()
    {
        RequireOwner("BeginFrame");
        if (FrameActive)
            throw std::logic_error("A render frame is already active.");
        FrameActive = true;
        Requests.clear();
        RuntimeUiCommands.clear();
        CpuPreparation.BeginFrame();
        ++Statistics.Frame;
        Statistics.Passes = 0;
        Statistics.Surfaces = 0;
        Statistics.DrawCalls = 0;
        Statistics.DepthDrawCalls = 0;
        Statistics.ShadowDrawCalls = 0;
        Statistics.Triangles = 0;
        Statistics.VisibleSubmeshes = 0;
        Statistics.CulledSubmeshes = 0;
        Statistics.CulledShadowSubmeshes = 0;
        Statistics.InstanceBatches = 0;
        Statistics.CulledLocalLights = 0;
        Statistics.VisibleLocalLights = 0;
        Statistics.OverflowedLightTiles = 0;
        Statistics.DirectionalShadowCascades = 0;
        Statistics.VfxSpriteParticles = 0;
        Statistics.VfxMeshParticles = 0;
        Statistics.VfxRibbonParticles = 0;
        Statistics.VfxVolumetricParticles = 0;
        Statistics.CulledCpuVfxParticles = 0;
        Statistics.DroppedVfxParticles = 0;
        Statistics.VfxComputeThreadGroups = 0;
        Statistics.VfxComputeDispatches = 0;
        Statistics.VfxIndirectDraws = 0;
        Statistics.CpuVfxDrawBatches = 0;
        Statistics.VfxGpuWorlds = 0;
        Statistics.VfxGpuParticleCapacity = 0;
        Statistics.VfxGpuBufferBytes = 0;
        Statistics.GpuFenceWaitMilliseconds = 0.0F;
        Statistics.SampledResolvedDepthAvailable = false;
        Statistics.PlannedFrameGraphPasses = static_cast<std::uint32_t>(SceneFrameGraph.Compiled.Order.size());
        Statistics.ExecutedFrameGraphPasses = 0;
        Statistics.FrameGraphTransitions = 0;
        Statistics.TransientResourceAllocations =
            static_cast<std::uint32_t>(SceneFrameGraph.Compiled.TransientAllocations.size());
        Statistics.ForwardPlusBufferReallocations = 0;
        Statistics.ForwardPlusUploadBytes = 0;
        Statistics.DynamicUploadBufferReallocations = 0;
        Statistics.DynamicUploadBytes = 0;
        Statistics.DepthTriangles = 0;
        Statistics.ShadowTriangles = 0;
        Statistics.GpuOcclusionCandidates = 0;
        Statistics.GpuOcclusionVisible = 0;
        Statistics.GpuOcclusionCulled = 0;
        Statistics.GpuOcclusionReadbackAge = std::numeric_limits<std::uint32_t>::max();
        Statistics.GpuOcclusionCandidateTriangles = 0;
        Statistics.GpuOcclusionCulledTriangles = 0;
        Statistics.GpuOcclusionReadbackValid = false;
        CollectCompletedFrames();
        const auto vfxRetirementAge = static_cast<std::uint64_t>(Specification.MaximumFramesInFlight) + 2U;
        for (auto iterator = GpuVfxWorlds.begin(); iterator != GpuVfxWorlds.end();)
        {
            if (iterator->second.LastPreparedFrame != 0 && Statistics.Frame > iterator->second.LastPreparedFrame &&
                Statistics.Frame - iterator->second.LastPreparedFrame > vfxRetirementAge)
            {
                ReleaseGpuVfxWorld(iterator->second);
                iterator = GpuVfxWorlds.erase(iterator);
                continue;
            }
            ++iterator;
        }
        Statistics.VfxGpuWorlds = static_cast<std::uint32_t>(GpuVfxWorlds.size());
        const auto skinRetirementAge = static_cast<std::uint64_t>(Specification.MaximumFramesInFlight) + 2U;
        for (auto cacheIterator = SkinCache.begin(); cacheIterator != SkinCache.end();)
        {
            auto& entry = cacheIterator->second;
            if (entry.LastRequestedFrame != 0 && Statistics.Frame > entry.LastRequestedFrame &&
                Statistics.Frame - entry.LastRequestedFrame > skinRetirementAge)
            {
                Retire(std::move(entry.Resources));
                cacheIterator = SkinCache.erase(cacheIterator);
                continue;
            }

            for (auto instanceIterator = entry.Resources.Instances.begin();
                 instanceIterator != entry.Resources.Instances.end();)
            {
                if (instanceIterator->second.LastPreparedFrame != 0 &&
                    Statistics.Frame > instanceIterator->second.LastPreparedFrame &&
                    Statistics.Frame - instanceIterator->second.LastPreparedFrame > skinRetirementAge)
                {
                    GpuSkinResources retired;
                    retired.Instances.emplace(instanceIterator->first, std::move(instanceIterator->second));
                    Retire(std::move(retired));
                    instanceIterator = entry.Resources.Instances.erase(instanceIterator);
                    continue;
                }
                ++instanceIterator;
            }
            ++cacheIterator;
        }
        for (const auto& surface : LiveSurfaces())
        {
            surface->Submitted = false;
            surface->FrameClearColor = surface->Specification.ClearColor;
            EnsureSurface(*surface);
        }
        // EnsureSurface can invalidate a completed readback during resize or resource recovery. Keep the frame
        // aggregate consistent with the per-surface state exposed to diagnostics during this frame.
        PublishGpuOcclusionReadbackStatistics();
    }

    void RenderSharedState::CancelFrame() noexcept
    {
        FrameActive = false;
        Requests.clear();
        RuntimeUiCommands.clear();
    }

    void RenderSharedState::Submit(SceneRenderRequest request)
    {
        const auto preparationStarted = std::chrono::steady_clock::now();
        RequireOwner("Submit");
        if (!FrameActive)
            throw std::logic_error("Scene render requests are accepted only during an active render frame.");
        if (!request.Scene || !request.View || !request.View->Surface())
            throw std::invalid_argument("SceneRenderRequest requires a scene, view, and render surface.");
        if (!request.Scene->IsOpen())
            throw std::logic_error("SceneRenderRequest cannot submit a closed scene.");

        auto& surface =
            *static_cast<RenderSurfaceState*>(RenderSystemInternalAccess::SurfaceState(*request.View->Surface()));
        const auto owner = surface.Owner.lock();
        if (owner.get() != this)
            throw std::invalid_argument("SceneRenderRequest surface belongs to another renderer.");
        if (surface.Submitted)
            throw std::logic_error("A render surface may receive only one scene request per frame.");
        if (Specification.Mode == RenderMode::Rendered && (!surface.Specification.Depth || !DepthFormat))
            throw std::logic_error("Scene rendering requires a depth-enabled render surface.");
        const auto camera = request.View->Camera();
        if (!Math::IsFinite(camera.View) || !Math::IsFinite(camera.Projection) || !ValidColor(camera.ClearColor))
            throw std::invalid_argument("SceneRenderRequest camera contains invalid values.");
        if (!ValidColor(request.Environment.AmbientColor) || !std::isfinite(request.Environment.AmbientIntensity) ||
            request.Environment.AmbientIntensity < 0.0F || request.Environment.AmbientIntensity > 16.0F ||
            !std::isfinite(request.Environment.Exposure) || request.Environment.Exposure < 0.01F ||
            request.Environment.Exposure > 16.0F)
        {
            throw std::invalid_argument("SceneRenderRequest environment contains invalid values.");
        }
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
        if (request.Vfx.Particles().size() > VfxRenderSnapshot::MaximumParticles)
            throw std::invalid_argument("SceneRenderRequest exceeds the VFX particle packet bound.");
        for (const auto& particle : request.Vfx.Particles())
        {
            if (!Math::IsFinite(particle.Position) || !Math::IsFinite(particle.PreviousPosition) ||
                !Math::IsFinite(particle.Rotation) || !Math::IsFinite(particle.Tint) || !std::isfinite(particle.Size) ||
                particle.Size < 0.0F || particle.Renderer > VfxRendererType::Volumetric)
            {
                throw std::invalid_argument("SceneRenderRequest contains an invalid VFX particle.");
            }
            if (particle.Renderer == VfxRendererType::Sprite)
                ++Statistics.VfxSpriteParticles;
            else if (particle.Renderer == VfxRendererType::Mesh)
                ++Statistics.VfxMeshParticles;
            else if (particle.Renderer == VfxRendererType::Ribbon)
                ++Statistics.VfxRibbonParticles;
            else
                ++Statistics.VfxVolumetricParticles;
        }
        if (!request.Vfx.GpuEmitters().empty() &&
            (request.Vfx.WorldId() == 0 || request.Vfx.ParticleCapacity() == 0 ||
             request.Vfx.ParticleCapacity() > 10'000'000U || !std::isfinite(request.Vfx.DeltaSeconds()) ||
             request.Vfx.DeltaSeconds() < 0.0F))
        {
            throw std::invalid_argument("SceneRenderRequest contains an invalid GPU VFX snapshot.");
        }
        for (const auto& emitter : request.Vfx.GpuEmitters())
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
                emitter.Capacity == 0 || emitter.Capacity > request.Vfx.ParticleCapacity() ||
                (emitter.Renderer == VfxRendererType::Mesh && !emitter.Mesh))
            {
                throw std::invalid_argument("SceneRenderRequest contains an invalid GPU VFX emitter.");
            }
        }
        Statistics.DroppedVfxParticles += request.Vfx.DroppedParticles();

        if (surface.GpuOcclusionSubmittedMode != request.Environment.GpuOcclusion)
        {
            surface.GpuOcclusionSubmittedMode = request.Environment.GpuOcclusion;
            surface.GpuOcclusionSubmissionEpoch =
                surface.GpuOcclusionSubmissionEpoch == std::numeric_limits<std::uint64_t>::max()
                    ? 1U
                    : surface.GpuOcclusionSubmissionEpoch + 1U;
            surface.GpuOcclusionDiagnostics = {};
            surface.GpuOcclusionDiagnostics.RequestedMode = request.Environment.GpuOcclusion;
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
        surface.FrameClearColor = camera.ClearColor;
        SceneRenderPacket packet;
        packet.Scene = request.Scene->Asset();
        packet.Camera = camera;
        packet.Environment = request.Environment;
        packet.GlobalMaterialProperties = std::move(request.GlobalMaterialProperties);
        packet.Lighting = ResolveLighting(request.Scene);
        packet.LocalLights = ResolveLocalLights(request.Scene);
        const auto lightFrustum = GeometryDetail::BuildFrustumPlanes(Math::Multiply(camera.Projection, camera.View));
        Statistics.CulledLocalLights += static_cast<std::uint32_t>(
            std::erase_if(packet.LocalLights,
                          [&](const SceneLocalLight& light)
                          {
                              const auto radius = std::max(light.Range, 0.0F);
                              const MeshBounds bounds{
                                  {light.Position.X - radius, light.Position.Y - radius, light.Position.Z - radius},
                                  {light.Position.X + radius, light.Position.Y + radius, light.Position.Z + radius}};
                              return !GeometryDetail::IntersectsFrustum(lightFrustum, bounds);
                          }));
        packet.BakedLighting = request.Scene->BakedLighting();
        packet.ReflectionProbes = ResolveReflectionProbes(request.Scene);
        packet.LightProbeVolumes = ResolveLightProbeVolumes(request.Scene);
        packet.DrawGrid = request.DrawGrid;
        packet.Vfx = std::move(request.Vfx);
        packet.MaterialTimeSeconds = request.MaterialTimeSeconds;
        packet.MaterialDeltaSeconds = request.MaterialDeltaSeconds;
        packet.FrameIndex = request.FrameIndex;
        const auto renderEntities = request.Scene->Query<MeshRendererComponent>();
        const auto meshParticleCount = std::ranges::count_if(packet.Vfx.Particles(),
                                                             [](const auto& particle)
                                                             {
                                                                 return particle.Renderer == VfxRendererType::Mesh &&
                                                                        static_cast<bool>(particle.Mesh) &&
                                                                        particle.Size > 0.0F;
                                                             });
        packet.DrawItems.reserve(renderEntities.size() + static_cast<std::size_t>(meshParticleCount));
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
            if (const auto animator = entity.GetComponent<AnimatorComponent>(); animator && animator->Enabled())
            {
                skinPalette.assign(animator->SkinPalette().begin(), animator->SkinPalette().end());
                skin = animator->SkinnedMesh();
                skinSkeleton = animator->Skeleton();
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
            packet.DrawItems.back().MaterialInstanceProperties = renderer->AllMaterialInstanceProperties();
        }
        for (const auto& particle : packet.Vfx.Particles())
        {
            if (auto item = VfxMeshDrawItem(particle))
                packet.DrawItems.push_back(std::move(*item));
        }
        Requests.push_back({std::move(packet), &surface});
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
            result.ReceivesShadows = definition.ReceivesShadows;
            result.DepthTest = definition.DepthTest;
            result.DepthWrite = definition.DepthWrite;
            result.UsesForwardPlus = definition.UsesForwardPlus;
            result.UsesInstancing = definition.UsesInstancing;
            result.UsesImageBasedLighting = definition.UsesImageBasedLighting;
            result.UsesSpatialLighting = definition.SpatialLightingAbiVersion == 2U;
            result.UsesVertexMaterialParameters = definition.UsesVertexMaterialParameters;
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
            KEIRE_CORE_ERROR("Material GPU binding rebuild failed for id={} revision={}: {}", id.ToString(),
                             cache.LoadedRevision, error.what());
        }
        return finishDependencyCheck(binding->Binding.Pipeline ? &binding->Binding : nullptr);
    }
} // namespace Keire::RenderBackend
