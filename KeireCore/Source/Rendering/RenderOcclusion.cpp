#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Diagnostics/TelemetryInternal.h"
#include "KeireInternal/Rendering/DisplacementBoundsInternal.h"
#include "KeireInternal/Rendering/GpuOcclusionPolicyInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"

#include "Keire/Log.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace Policy = Keire::RenderBackend::GpuOcclusionPolicy;

    constexpr float OcclusionDepthBias = 0.0001F;
    constexpr std::uint32_t ForceVisibleFlag =
        static_cast<std::uint32_t>(Keire::RenderBackend::GpuVisibilityFlags::ForceVisible);
    constexpr std::uint32_t MaximumGpuOcclusionCandidates = 262'144U;
    constexpr std::uint32_t MaximumGpuOcclusionSurfaceDimension = 16'384U;

    class ScopedGpuDebugGroup final
    {
      public:
        ScopedGpuDebugGroup(SDL_GPUCommandBuffer* commands, const char* name) noexcept : m_Commands(commands)
        {
            if (m_Commands)
                SDL_PushGPUDebugGroup(m_Commands, name);
        }

        ~ScopedGpuDebugGroup()
        {
            if (m_Commands)
                SDL_PopGPUDebugGroup(m_Commands);
        }

        ScopedGpuDebugGroup(const ScopedGpuDebugGroup&) = delete;
        ScopedGpuDebugGroup& operator=(const ScopedGpuDebugGroup&) = delete;

      private:
        SDL_GPUCommandBuffer* m_Commands = nullptr;
    };

    [[nodiscard]] std::uint32_t GrowCapacity(const std::uint64_t required)
    {
        if (required == 0 || required > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("GPU occlusion resource exceeds SDL's 32-bit size limit.");
        const auto minimum = std::max<std::uint64_t>(required, 256U);
        const auto capacity = std::bit_ceil(minimum);
        if (capacity > std::numeric_limits<std::uint32_t>::max())
            return std::numeric_limits<std::uint32_t>::max();
        return static_cast<std::uint32_t>(capacity);
    }

    [[nodiscard]] std::size_t AlignUpload(const std::size_t value) noexcept
    {
        return (value + 15U) & ~std::size_t{15U};
    }

    [[nodiscard]] SDL_GPUCullMode
    OcclusionCullMode(const Keire::RenderBackend::ResolvedAssetMaterial& material) noexcept
    {
        if (material.Surface.DoubleSided)
            return SDL_GPU_CULLMODE_NONE;
        switch (material.Culling)
        {
        case Keire::ShaderCullMode::Front:
            return SDL_GPU_CULLMODE_FRONT;
        case Keire::ShaderCullMode::Back:
            return SDL_GPU_CULLMODE_BACK;
        case Keire::ShaderCullMode::None:
        default:
            return SDL_GPU_CULLMODE_NONE;
        }
    }

    [[nodiscard]] std::size_t OcclusionCullPipelineIndex(const SDL_GPUCullMode mode) noexcept
    {
        return mode == SDL_GPU_CULLMODE_FRONT ? 1U : mode == SDL_GPU_CULLMODE_BACK ? 2U : 0U;
    }

    [[nodiscard]] bool PublishFallback(Keire::RenderBackend::RenderSurfaceState& surface,
                                       const Keire::GpuOcclusionMode requested,
                                       const Keire::GpuOcclusionFallbackReason reason)
    {
        return Keire::RenderBackend::PublishGpuOcclusionFallback(surface, requested, reason);
    }

    void PublishStandby(Keire::RenderBackend::RenderSurfaceState& surface, const Keire::GpuOcclusionMode requested,
                        const Keire::GpuOcclusionFallbackReason reason)
    {
        (void)PublishFallback(surface, requested, reason);
        surface.GpuOcclusionDiagnostics.State = Keire::GpuOcclusionSurfaceState::Idle;
    }

    [[nodiscard]] Keire::RenderBackend::GpuVisibilityClass
    ResolvedVisibilityClass(const Keire::RenderBackend::SceneDrawItem& item) noexcept
    {
        const bool skinned = static_cast<bool>(item.Skin) ||
                             item.VisibilityClass == Keire::RenderBackend::GpuVisibilityClass::SkinnedMesh;
        return Keire::RenderBackend::GpuVisibilityClassForDraw(
            skinned, item.VisibilityClass == Keire::RenderBackend::GpuVisibilityClass::MeshVfx);
    }

    [[nodiscard]] std::uint32_t VisibilityMetadataFlags(const Keire::RenderBackend::SceneDrawItem& item,
                                                        const std::uint64_t frameIndex,
                                                        const std::size_t submeshCount) noexcept
    {
        const bool freshDynamicBounds = item.HasFreshCurrentPoseBounds(frameIndex, submeshCount);
        return static_cast<std::uint32_t>(Keire::RenderBackend::GpuVisibilityFlagsForDraw(
            ResolvedVisibilityClass(item), item.AlwaysVisible, freshDynamicBounds));
    }

    [[nodiscard]] bool VisibilityResourcesOwnedBy(const Keire::RenderBackend::GpuOcclusionFrameResources& resources,
                                                  const Keire::RenderBackend::RenderSurfaceState& surface,
                                                  const Keire::RenderBackend::SceneRenderPacket& packet,
                                                  const std::uint32_t deviceGeneration) noexcept
    {
        return resources.OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch, deviceGeneration);
    }
} // namespace

namespace Keire::RenderBackend
{
    PreparedGpuOcclusion RenderSharedState::PrepareGpuOcclusion(SDL_GPUCommandBuffer* commands,
                                                                RenderSurfaceState& surface,
                                                                const SceneRenderPacket& packet,
                                                                PreparedSceneDrawLists& draws)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion prepare");
        PreparedGpuOcclusion prepared;
        const auto requested = packet.Environment.GpuOcclusion;
        const bool debugValidation =
            requested == GpuOcclusionMode::Automatic &&
            surface.GpuOcclusionDebugMode.load(std::memory_order_acquire) == GpuOcclusionDebugView::VisibilityBounds;
        const auto effective = debugValidation ? GpuOcclusionMode::Forced : requested;
        for (const auto& item : packet.DrawItems)
        {
            const auto visibilityClass = ResolvedVisibilityClass(item);
            const auto submeshCount = ResolveMesh(item.Mesh).Submeshes.size();
            if (visibilityClass == GpuVisibilityClass::MeshVfx)
                ++Statistics.GpuOcclusionMeshVfxCandidates;
            else if (visibilityClass == GpuVisibilityClass::SkinnedMesh)
                ++Statistics.GpuOcclusionSkinnedMeshCandidates;
            else
                ++Statistics.GpuOcclusionStaticMeshCandidates;
            if (HasGpuVisibilityFlag(
                    GpuVisibilityFlagsForDraw(visibilityClass, item.AlwaysVisible,
                                              item.HasFreshCurrentPoseBounds(packet.FrameIndex, submeshCount)),
                    GpuVisibilityFlags::ForceVisible))
            {
                ++Statistics.GpuOcclusionForcedVisibleCandidates;
            }
        }
        const auto localLightCandidates = static_cast<std::uint32_t>(packet.LocalLights.size());
        const auto spatialVolumeCandidates =
            static_cast<std::uint32_t>(packet.ReflectionProbes.size() + packet.LightProbeVolumes.size());
        std::uint32_t vfxVisibilityCandidates = 0;
        Statistics.GpuOcclusionLocalLightCandidates += localLightCandidates;
        Statistics.GpuOcclusionSpatialVolumeCandidates += spatialVolumeCandidates;
        Statistics.GpuOcclusionForcedVisibleCandidates += localLightCandidates + spatialVolumeCandidates;
        auto& diagnostics = surface.GpuOcclusionDiagnostics;
        diagnostics.RequestedMode = requested;
        diagnostics.EligibleCandidates = 0;
        diagnostics.EligibleSafeOccluders = 0;
        diagnostics.EligibleCandidateTriangles = 0;
        if (requested == GpuOcclusionMode::Disabled)
        {
            (void)PublishFallback(surface, requested, GpuOcclusionFallbackReason::DisabledBySetting);
            surface.GpuOcclusionAutomaticActive = false;
            surface.GpuOcclusionAutomaticQualifyingFrames = 0;
            surface.GpuOcclusionAutomaticMinimumFrames = 0;
            surface.GpuOcclusionAutomaticCooldownFrames = 0;
            surface.GpuOcclusionAutomaticUnprofitableActivations = 0;
            surface.GpuOcclusionValidationCooldown = false;
            surface.GpuOcclusionValidationFallbackEventPending = false;
            Policy::ResetAllocationRetry(surface.GpuOcclusionAllocationRetry);
            return prepared;
        }
        if (surface.GpuOcclusionValidationCooldown && surface.GpuOcclusionAutomaticCooldownFrames > 0U)
        {
            --surface.GpuOcclusionAutomaticCooldownFrames;
            if (surface.GpuOcclusionAutomaticCooldownFrames == 0U)
                surface.GpuOcclusionValidationCooldown = false;
            const bool transitioned =
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::ReadbackValidationFailed);
            Statistics.GpuOcclusionFallbacks +=
                ConsumeGpuOcclusionValidationFallbackEvent(surface, transitioned) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return prepared;
        }
        if (surface.Width == 0U || surface.Height == 0U || surface.Width > MaximumGpuOcclusionSurfaceDimension ||
            surface.Height > MaximumGpuOcclusionSurfaceDimension)
        {
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::ResourceAllocationFailed) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            surface.GpuOcclusionAutomaticActive = false;
            surface.GpuOcclusionAutomaticQualifyingFrames = 0U;
            surface.GpuOcclusionAutomaticMinimumFrames = 0U;
            Policy::ResetAllocationRetry(surface.GpuOcclusionAllocationRetry);
            return prepared;
        }
        const auto resourceExtent = Policy::ResolveConservativeResourceExtent(surface.Width, surface.Height);
        const auto frameSlotCount = static_cast<std::size_t>(Specification.MaximumFramesInFlight);
        const auto frameSlot = surface.ActiveWorksetSlot;
        const auto frameIndex = static_cast<std::size_t>(frameSlot);
        if (frameIndex >= frameSlotCount)
            throw std::logic_error("GPU occlusion frame slot exceeds the accepted frame bound.");
        const auto textureBytes = Policy::EstimateTextureMemoryBytes(resourceExtent, MaximumGpuOcclusionPyramidLevels,
                                                                     Specification.MaximumFramesInFlight);
        if (!textureBytes || !Policy::TextureMemoryWithinBudget(*textureBytes))
        {
            const bool transitioned =
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::ResourceAllocationFailed);
            Statistics.GpuOcclusionFallbacks += transitioned ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            surface.GpuOcclusionAutomaticActive = false;
            surface.GpuOcclusionAutomaticQualifyingFrames = 0U;
            surface.GpuOcclusionAutomaticMinimumFrames = 0U;
            Policy::ResetAllocationRetry(surface.GpuOcclusionAllocationRetry);
            if (transitioned)
            {
                if (textureBytes)
                {
                    KEIRE_CORE_WARN(
                        "GPU occlusion textures for surface '{}' require {} bytes across {} frame slots, exceeding "
                        "the per-surface budget of {} bytes; retaining direct draws.",
                        surface.Specification.Name, *textureBytes, Specification.MaximumFramesInFlight,
                        Policy::MaximumTextureBytesPerSurface);
                }
                else
                {
                    KEIRE_CORE_WARN(
                        "GPU occlusion texture accounting overflowed for surface '{}'; retaining direct draws.",
                        surface.Specification.Name);
                }
            }
            return prepared;
        }
        Policy::PrepareAllocationRetryExtent(surface.GpuOcclusionAllocationRetry, resourceExtent);
        if (GpuOcclusionPipelinesAttempted && !GpuOcclusionPipelineFailure.empty())
        {
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::PipelineUnavailable) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return prepared;
        }
        if (!GpuOcclusionCapability.load(std::memory_order_acquire))
        {
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::UnsupportedBackend) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return prepared;
        }
        if (!EnsureGpuOcclusionPipelines())
        {
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::PipelineUnavailable) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return prepared;
        }
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        std::vector<GpuOcclusionCandidate> candidates;
        std::vector<GpuInstanceUniform> inputInstances;
        std::vector<GpuOcclusionChunk> chunks;
        std::vector<GpuOcclusionBatch> batches;
        std::vector<SDL_GPUIndexedIndirectDrawCommand> indirect;
        std::vector<PreparedSceneBatch*> preparedBatches;
        std::array<bool, Policy::AutomaticCoverageColumns * Policy::AutomaticCoverageRows> occluderCoverage{};
        std::uint64_t depthTriangles = 0;
        bool oversizedBatch = false;
        bool legacyShaderAbi = false;
        candidates.reserve(draws.Opaque.Draws.size());
        inputInstances.reserve(draws.Opaque.Draws.size());
        preparedBatches.reserve(draws.Opaque.Batches.size());
        for (std::size_t sceneBatchIndex = 0; sceneBatchIndex < draws.Opaque.Batches.size(); ++sceneBatchIndex)
        {
            auto& sceneBatch = draws.Opaque.Batches[sceneBatchIndex];
            const auto drawIndex = static_cast<std::size_t>(sceneBatch.First);
            const auto& draw = draws.Opaque.Draws[drawIndex];
            const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
            const bool builtInFallback = !draw.Material;
            if ((!material && !builtInFallback) ||
                (material && (material->Topology != ShaderPrimitiveTopology::TriangleList || !material->DepthTest ||
                              IsTransparentMaterial(material->Surface.AlphaMode))))
            {
                continue;
            }
            if (material && material->InstanceAddressingAbiVersion != 2U)
            {
                legacyShaderAbi = true;
                continue;
            }
            if (material &&
                !HasShaderOcclusionSupport(material->OcclusionSupport, ShaderOcclusionSupport::ConservativeBounds))
            {
                continue;
            }
            const std::optional<float> maximumDisplacement =
                material ? material->MaximumWorldPositionDisplacementRadius : std::optional<float>{0.0F};
            if (!DisplacementBounds::IsKnown(maximumDisplacement))
                continue;
            const auto displacementRadius = *maximumDisplacement;
            if (sceneBatch.Count > MaximumGpuOcclusionBatchInstances)
            {
                oversizedBatch = true;
                continue;
            }
            if (sceneBatch.Count > MaximumGpuOcclusionCandidates - candidates.size())
            {
                oversizedBatch = true;
                continue;
            }
            const auto candidateFirst = static_cast<std::uint32_t>(candidates.size());
            for (std::uint32_t instance = 0; instance < sceneBatch.Count; ++instance)
            {
                const auto& instanceDraw = draws.Opaque.Draws[drawIndex + instance];
                const auto outputIndex = static_cast<std::uint32_t>(candidates.size());
                const auto submeshCount = ResolveMesh(instanceDraw.Item->Mesh).Submeshes.size();
                candidates.push_back(
                    {{instanceDraw.Submesh.Bounds.Minimum.X, instanceDraw.Submesh.Bounds.Minimum.Y,
                      instanceDraw.Submesh.Bounds.Minimum.Z, displacementRadius},
                     {instanceDraw.Submesh.Bounds.Maximum.X, instanceDraw.Submesh.Bounds.Maximum.Y,
                      instanceDraw.Submesh.Bounds.Maximum.Z, displacementRadius},
                     {VisibilityMetadataFlags(*instanceDraw.Item, packet.FrameIndex, submeshCount),
                      static_cast<std::uint32_t>(ResolvedVisibilityClass(*instanceDraw.Item)), outputIndex,
                      static_cast<std::uint32_t>(GpuVisibilityConsumer::IndexedIndirect)}});
                if (ResolvedVisibilityClass(*instanceDraw.Item) == GpuVisibilityClass::SkinnedMesh &&
                    instanceDraw.Item->HasFreshCurrentPoseBounds(packet.FrameIndex, submeshCount))
                {
                    ++prepared.FreshPoseSkinnedCandidates;
                }
                inputInstances.push_back({instanceDraw.Item->World, Transpose(Math::Inverse(instanceDraw.Item->World)),
                                          instanceDraw.Item->Tint});
            }
            const auto chunkFirst = static_cast<std::uint32_t>(chunks.size());
            for (std::uint32_t first = 0; first < sceneBatch.Count; first += GpuOcclusionScanBlockSize)
            {
                chunks.push_back({candidateFirst + first, std::min(GpuOcclusionScanBlockSize, sceneBatch.Count - first),
                                  static_cast<std::uint32_t>(batches.size()), 0U});
            }
            const auto triangleCount = draw.Submesh.IndexCount / 3U;
            const auto indirectOffset =
                static_cast<std::uint32_t>(indirect.size() * sizeof(SDL_GPUIndexedIndirectDrawCommand));
            batches.push_back({candidateFirst, sceneBatch.Count, candidateFirst, chunkFirst,
                               static_cast<std::uint32_t>(chunks.size()) - chunkFirst, indirectOffset, triangleCount,
                               0U});
            indirect.push_back({draw.Submesh.IndexCount, 0U, draw.Submesh.FirstIndex, 0, 0U});
            preparedBatches.push_back(std::addressof(sceneBatch));
            prepared.CandidateTriangles += static_cast<std::uint64_t>(triangleCount) * sceneBatch.Count;

            const bool depthCompatible =
                builtInFallback ||
                (material->Surface.AlphaMode == MaterialAlphaMode::Opaque && material->DepthWrite &&
                 HasShaderOcclusionSupport(material->OcclusionSupport, ShaderOcclusionSupport::DepthOnlyGeometryMatch));
            if (!depthCompatible)
                continue;
            std::uint32_t rangeFirst = 0;
            std::uint32_t rangeCount = 0;
            const auto flushRange = [&]
            {
                if (rangeCount == 0U)
                    return;
                prepared.Occluders.push_back({static_cast<std::uint32_t>(sceneBatchIndex), rangeFirst, rangeCount,
                                              material ? OcclusionCullMode(*material) : SDL_GPU_CULLMODE_BACK});
                rangeCount = 0U;
            };
            for (std::uint32_t instance = 0; instance < sceneBatch.Count; ++instance)
            {
                const auto& instanceDraw = draws.Opaque.Draws[drawIndex + instance];
                const auto submeshCount = ResolveMesh(instanceDraw.Item->Mesh).Submeshes.size();
                if (!CanGpuVisibilityClassOcclude(
                        ResolvedVisibilityClass(*instanceDraw.Item),
                        instanceDraw.Item->HasFreshCurrentPoseBounds(packet.FrameIndex, submeshCount)))
                {
                    flushRange();
                    continue;
                }
                const auto clipFromLocal = Math::Multiply(packet.Camera.Projection,
                                                          Math::Multiply(packet.Camera.View, instanceDraw.Item->World));
                const auto rectangle = GeometryDetail::ProjectedBoundsPixels(
                    clipFromLocal, instanceDraw.Submesh.Bounds, resourceExtent.Width, resourceExtent.Height);
                const float area = rectangle.Area();
                const float minimumOccluderPixels = effective == GpuOcclusionMode::Automatic
                                                        ? Policy::AutomaticMinimumOccluderPixels
                                                        : Policy::ForcedMinimumOccluderPixels;
                if (area < minimumOccluderPixels)
                {
                    flushRange();
                    continue;
                }
                const auto gpuInstance = candidateFirst + instance;
                candidates[gpuInstance].Metadata[0] |= ForceVisibleFlag;
                if (rangeCount == 0U)
                    rangeFirst = gpuInstance;
                ++rangeCount;
                depthTriangles += triangleCount;
                for (std::uint32_t row = 0; row < Policy::AutomaticCoverageRows; ++row)
                {
                    const float cellMinimumY = static_cast<float>(row) * static_cast<float>(resourceExtent.Height) /
                                               static_cast<float>(Policy::AutomaticCoverageRows);
                    const float cellMaximumY = static_cast<float>(row + 1U) *
                                               static_cast<float>(resourceExtent.Height) /
                                               static_cast<float>(Policy::AutomaticCoverageRows);
                    for (std::uint32_t column = 0; column < Policy::AutomaticCoverageColumns; ++column)
                    {
                        const float cellMinimumX = static_cast<float>(column) *
                                                   static_cast<float>(resourceExtent.Width) /
                                                   static_cast<float>(Policy::AutomaticCoverageColumns);
                        const float cellMaximumX = static_cast<float>(column + 1U) *
                                                   static_cast<float>(resourceExtent.Width) /
                                                   static_cast<float>(Policy::AutomaticCoverageColumns);
                        if (rectangle.MinimumX <= cellMinimumX && rectangle.MaximumX >= cellMaximumX &&
                            rectangle.MinimumY <= cellMinimumY && rectangle.MaximumY >= cellMaximumY)
                        {
                            occluderCoverage[row * Policy::AutomaticCoverageColumns + column] = true;
                        }
                    }
                }
            }
            flushRange();
        }

        // Transparent draws never contribute occluder depth. A singleton with depth testing and conservative bounds
        // can still be rejected safely against the opaque HZB without changing back-to-front ordering.
        for (auto& sceneBatch : draws.Transparent.Batches)
        {
            if (sceneBatch.Count != 1U)
                continue;
            const auto drawIndex = static_cast<std::size_t>(sceneBatch.First);
            const auto& draw = draws.Transparent.Draws[drawIndex];
            const auto* material = draw.Material ? ResolveAssetMaterial(draw.Material, samples) : nullptr;
            if (!material || material->Topology != ShaderPrimitiveTopology::TriangleList || !material->DepthTest ||
                !IsTransparentMaterial(material->Surface.AlphaMode))
            {
                continue;
            }
            if (material->InstanceAddressingAbiVersion != 2U)
            {
                legacyShaderAbi = true;
                continue;
            }
            if (!HasShaderOcclusionSupport(material->OcclusionSupport, ShaderOcclusionSupport::ConservativeBounds))
            {
                continue;
            }
            if (!DisplacementBounds::IsKnown(material->MaximumWorldPositionDisplacementRadius))
                continue;
            const auto displacementRadius = *material->MaximumWorldPositionDisplacementRadius;
            if (candidates.size() >= MaximumGpuOcclusionCandidates)
            {
                oversizedBatch = true;
                continue;
            }
            const auto candidateFirst = static_cast<std::uint32_t>(candidates.size());
            const auto submeshCount = ResolveMesh(draw.Item->Mesh).Submeshes.size();
            candidates.push_back({{draw.Submesh.Bounds.Minimum.X, draw.Submesh.Bounds.Minimum.Y,
                                   draw.Submesh.Bounds.Minimum.Z, displacementRadius},
                                  {draw.Submesh.Bounds.Maximum.X, draw.Submesh.Bounds.Maximum.Y,
                                   draw.Submesh.Bounds.Maximum.Z, displacementRadius},
                                  {VisibilityMetadataFlags(*draw.Item, packet.FrameIndex, submeshCount),
                                   static_cast<std::uint32_t>(ResolvedVisibilityClass(*draw.Item)), candidateFirst,
                                   static_cast<std::uint32_t>(GpuVisibilityConsumer::IndexedIndirect)}});
            if (ResolvedVisibilityClass(*draw.Item) == GpuVisibilityClass::SkinnedMesh &&
                draw.Item->HasFreshCurrentPoseBounds(packet.FrameIndex, submeshCount))
            {
                ++prepared.FreshPoseSkinnedCandidates;
            }
            inputInstances.push_back({draw.Item->World, Transpose(Math::Inverse(draw.Item->World)), draw.Item->Tint});
            const auto chunkFirst = static_cast<std::uint32_t>(chunks.size());
            chunks.push_back({candidateFirst, 1U, static_cast<std::uint32_t>(batches.size()), 0U});
            const auto triangleCount = draw.Submesh.IndexCount / 3U;
            const auto indirectOffset =
                static_cast<std::uint32_t>(indirect.size() * sizeof(SDL_GPUIndexedIndirectDrawCommand));
            batches.push_back({candidateFirst, 1U, candidateFirst, chunkFirst, 1U, indirectOffset, triangleCount, 0U});
            indirect.push_back({draw.Submesh.IndexCount, 0U, draw.Submesh.FirstIndex, 0, 0U});
            preparedBatches.push_back(std::addressof(sceneBatch));
            prepared.CandidateTriangles += triangleCount;
        }

        prepared.CandidateCount = static_cast<std::uint32_t>(candidates.size());
        const auto cameraWorld = Math::TransformPoint(Math::Inverse(packet.Camera.View), {});
        const auto cameraInsideSphere = [&](const Vector3 center, const float radius) noexcept
        {
            const auto delta = Vector3{cameraWorld.X - center.X, cameraWorld.Y - center.Y, cameraWorld.Z - center.Z};
            return !Math::IsFinite(cameraWorld) ||
                   delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z <= radius * radius;
        };
        const auto appendMaskCandidate =
            [&](const MeshBounds bounds, const Matrix4& model, const GpuVisibilityClass visibilityClass,
                const GpuVisibilityConsumer consumer, const std::uint32_t outputIndex, const bool forceVisible)
        {
            if (candidates.size() >= MaximumGpuOcclusionCandidates || !Math::IsFinite(bounds.Minimum) ||
                !Math::IsFinite(bounds.Maximum) || !Math::IsFinite(model))
            {
                return;
            }
            const auto flags = GpuVisibilityFlagsForDraw(visibilityClass, forceVisible, true);
            candidates.push_back({{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z, 0.0F},
                                  {bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z, 0.0F},
                                  {static_cast<std::uint32_t>(flags), static_cast<std::uint32_t>(visibilityClass),
                                   outputIndex, static_cast<std::uint32_t>(consumer)}});
            inputInstances.push_back({model, Transpose(Math::Inverse(model)), Color{}});
        };
        for (std::uint32_t index = 0; index < localLightCandidates; ++index)
        {
            const auto& light = packet.LocalLights[index];
            if (!std::isfinite(light.Range) || light.Range <= 0.0F || !Math::IsFinite(light.Position))
                continue;
            const Vector3 extent{light.Range, light.Range, light.Range};
            appendMaskCandidate({{-extent.X, -extent.Y, -extent.Z}, extent},
                                Math::ComposeTransform(light.Position, {}, {1.0F, 1.0F, 1.0F}),
                                light.Type == SceneLocalLightType::Spot ? GpuVisibilityClass::SpotLight
                                                                        : GpuVisibilityClass::PointLight,
                                GpuVisibilityConsumer::ForwardPlusLightMask, index,
                                cameraInsideSphere(light.Position, light.Range));
        }
        std::uint32_t spatialOutputIndex = 0;
        for (const auto& probe : packet.ReflectionProbes)
        {
            const auto blend = std::max(probe.BlendDistance, 0.0F);
            const Vector3 extent{probe.BoxExtents.X + blend, probe.BoxExtents.Y + blend, probe.BoxExtents.Z + blend};
            const auto localCamera = Math::TransformPoint(probe.WorldToLocal, cameraWorld);
            const bool cameraInside = !Math::IsFinite(localCamera) ||
                                      (std::abs(localCamera.X) <= extent.X && std::abs(localCamera.Y) <= extent.Y &&
                                       std::abs(localCamera.Z) <= extent.Z);
            appendMaskCandidate({{-extent.X, -extent.Y, -extent.Z}, extent}, probe.LocalToWorld,
                                GpuVisibilityClass::ReflectionProbe, GpuVisibilityConsumer::SpatialVolumeMask,
                                spatialOutputIndex, cameraInside);
            ++spatialOutputIndex;
        }
        for (const auto& volume : packet.LightProbeVolumes)
        {
            const auto localCamera = Math::TransformPoint(volume.WorldToLocal, cameraWorld);
            const bool cameraInside = !Math::IsFinite(localCamera) || (std::abs(localCamera.X) <= volume.BoxExtents.X &&
                                                                       std::abs(localCamera.Y) <= volume.BoxExtents.Y &&
                                                                       std::abs(localCamera.Z) <= volume.BoxExtents.Z);
            appendMaskCandidate({{-volume.BoxExtents.X, -volume.BoxExtents.Y, -volume.BoxExtents.Z}, volume.BoxExtents},
                                volume.LocalToWorld, GpuVisibilityClass::LightProbeVolume,
                                GpuVisibilityConsumer::SpatialVolumeMask, spatialOutputIndex, cameraInside);
            ++spatialOutputIndex;
        }
        std::vector<std::vector<CpuVfxVisibilityInput>> cpuVfxVisibility(packet.VfxSnapshots.size());
        std::vector<std::vector<GpuVfxVisibilityInput>> gpuVfxVisibility(packet.VfxSnapshots.size());
        const auto hasConservativeBounds = [&](const VfxGpuEmitter& emitter)
        {
            if (emitter.Renderer == VfxRendererType::Sprite || emitter.Renderer == VfxRendererType::Ribbon)
                return true;
            if (emitter.Renderer != VfxRendererType::Mesh)
                return false;
            const auto& mesh = ResolveMesh(emitter.Mesh);
            if (mesh.Empty() || !mesh.BoundsEncloseSubmeshes || !Math::IsFinite(mesh.Bounds.Minimum) ||
                !Math::IsFinite(mesh.Bounds.Maximum))
            {
                return false;
            }
            const auto* material = emitter.Material ? ResolveAssetMaterial(emitter.Material, samples) : nullptr;
            return material && DisplacementBounds::IsKnown(material->MaximumWorldPositionDisplacementRadius) &&
                   HasShaderOcclusionSupport(material->OcclusionSupport, ShaderOcclusionSupport::ConservativeBounds);
        };
        for (std::size_t snapshotIndex = 0; snapshotIndex < packet.VfxSnapshots.size(); ++snapshotIndex)
        {
            const auto& snapshot = packet.VfxSnapshots[snapshotIndex];
            auto& cpuInputs = cpuVfxVisibility[snapshotIndex];
            cpuInputs.reserve(snapshot.Particles().size());
            for (const auto& particle : snapshot.Particles())
                cpuInputs.push_back({particle.Renderer});
            auto& gpuInputs = gpuVfxVisibility[snapshotIndex];
            gpuInputs.reserve(snapshot.GpuEmitters().size());
            for (const auto& emitter : snapshot.GpuEmitters())
            {
                gpuInputs.push_back({{emitter.Handle.Index(), emitter.Handle.Generation()},
                                     emitter.Renderer,
                                     emitter.Capacity,
                                     hasConservativeBounds(emitter)});
            }
        }
        std::vector<VfxVisibilitySnapshotInput> vfxSnapshots;
        vfxSnapshots.reserve(packet.VfxSnapshots.size());
        for (std::size_t snapshotIndex = 0; snapshotIndex < packet.VfxSnapshots.size(); ++snapshotIndex)
            vfxSnapshots.push_back({cpuVfxVisibility[snapshotIndex], gpuVfxVisibility[snapshotIndex]});
        prepared.VfxCandidateFirst = static_cast<std::uint32_t>(candidates.size());
        const auto remainingCandidateCapacity =
            MaximumGpuOcclusionCandidates - static_cast<std::uint32_t>(candidates.size());
        prepared.VfxVisibility = BuildVfxVisibilityPlan(vfxSnapshots, remainingCandidateCapacity);
        vfxVisibilityCandidates = prepared.VfxVisibility.CandidateCount;
        for (const auto& entry : prepared.VfxVisibility.Entries)
        {
            if (entry.Disposition != VfxVisibilityPlanDisposition::CandidateRange)
                continue;
            const auto visibilityClass = entry.Renderer == VfxRendererType::Ribbon ? GpuVisibilityClass::RibbonVfx
                                         : entry.Renderer == VfxRendererType::Mesh ? GpuVisibilityClass::MeshVfx
                                                                                   : GpuVisibilityClass::SpriteVfx;
            for (std::uint32_t index = 0; index < entry.Count; ++index)
            {
                candidates.push_back(
                    {{},
                     {},
                     {ForceVisibleFlag, static_cast<std::uint32_t>(visibilityClass), entry.First + index,
                      static_cast<std::uint32_t>(GpuVisibilityConsumer::VfxVisibilityMask)}});
                inputInstances.push_back({Matrix4{}, Matrix4{}, Color{}});
            }
        }
        prepared.ClassificationCandidateCount = static_cast<std::uint32_t>(candidates.size());
        prepared.BatchCount = static_cast<std::uint32_t>(batches.size());
        prepared.ChunkCount = static_cast<std::uint32_t>(chunks.size());
        diagnostics.EligibleCandidates = prepared.CandidateCount;
        diagnostics.EligibleCandidateTriangles = prepared.CandidateTriangles;
        if (!diagnostics.ReadbackValid)
        {
            diagnostics.Candidates = prepared.CandidateCount;
            diagnostics.Visible = 0;
            diagnostics.Culled = 0;
        }
        diagnostics.SafeOccluders = 0;
        for (const auto& occluder : prepared.Occluders)
        {
            diagnostics.SafeOccluders += occluder.InstanceCount;
            diagnostics.EligibleSafeOccluders += occluder.InstanceCount;
        }
        const bool visibilityBoundsRequested =
            surface.GpuOcclusionDebugMode.load(std::memory_order_acquire) == GpuOcclusionDebugView::VisibilityBounds;
        const bool debugBoundsOnly = Policy::RequiresConservativeVisibilityDebugUpload(
            visibilityBoundsRequested, prepared.CandidateCount, !prepared.Occluders.empty());
        if (prepared.CandidateCount == 0)
        {
            const auto reason = oversizedBatch    ? GpuOcclusionFallbackReason::OversizedBatch
                                : legacyShaderAbi ? GpuOcclusionFallbackReason::LegacyShaderAbi
                                                  : GpuOcclusionFallbackReason::NoEligibleCandidates;
            if (reason == GpuOcclusionFallbackReason::NoEligibleCandidates && draws.Opaque.Draws.empty())
            {
                PublishStandby(surface, requested, reason);
                return {};
            }
            Statistics.GpuOcclusionFallbacks += PublishFallback(surface, requested, reason) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return {};
        }
        if (prepared.Occluders.empty())
        {
            if (effective == GpuOcclusionMode::Automatic && !debugBoundsOnly)
            {
                PublishStandby(surface, requested, GpuOcclusionFallbackReason::NoSafeOccluders);
                return {};
            }
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::NoSafeOccluders) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            if (!debugBoundsOnly)
                return {};
        }

        if (effective == GpuOcclusionMode::Automatic)
        {
            if (surface.GpuOcclusionAutomaticCooldownFrames > 0U)
            {
                --surface.GpuOcclusionAutomaticCooldownFrames;
                PublishStandby(surface, requested, GpuOcclusionFallbackReason::BelowAutomaticThreshold);
                return {};
            }
            const auto coveredCells = static_cast<std::uint32_t>(std::ranges::count(occluderCoverage, true));
            const float occluderCoverageRatio =
                static_cast<float>(coveredCells) /
                static_cast<float>(Policy::AutomaticCoverageColumns * Policy::AutomaticCoverageRows);
            const bool qualifies =
                prepared.CandidateCount >= Policy::AutomaticMinimumCandidates &&
                prepared.CandidateTriangles >= Policy::AutomaticMinimumCandidateTriangles &&
                occluderCoverageRatio >= Policy::AutomaticMinimumOccluderCoverage &&
                static_cast<double>(depthTriangles) <=
                    static_cast<double>(prepared.CandidateTriangles) * Policy::AutomaticMaximumDepthCostRatio;
            if (Policy::AnyAllocationRetryPending(surface.GpuOcclusionAllocationRetry))
            {
                if (qualifies && Policy::AllocationRetryPending(surface.GpuOcclusionAllocationRetry, frameIndex))
                {
                    (void)Policy::BeginAllocationAttempt(surface.GpuOcclusionAllocationRetry, frameIndex);
                }
                surface.GpuOcclusionAutomaticActive = false;
                surface.GpuOcclusionAutomaticQualifyingFrames = 0U;
                surface.GpuOcclusionAutomaticMinimumFrames = 0U;
                Statistics.GpuOcclusionFallbacks +=
                    PublishFallback(surface, requested, GpuOcclusionFallbackReason::ResourceAllocationFailed) ? 1U : 0U;
                Statistics.GpuOcclusionFallbackActive = true;
                return prepared;
            }
            if (surface.GpuOcclusionAutomaticActive)
            {
                if (surface.GpuOcclusionAutomaticMinimumFrames > 0)
                    --surface.GpuOcclusionAutomaticMinimumFrames;
                else if (!qualifies)
                {
                    surface.GpuOcclusionAutomaticActive = false;
                    surface.GpuOcclusionAutomaticQualifyingFrames = 0;
                }
            }
            else if (qualifies)
            {
                ++surface.GpuOcclusionAutomaticQualifyingFrames;
                if (surface.GpuOcclusionAutomaticQualifyingFrames >= Policy::AutomaticQualifyingFrames)
                {
                    surface.GpuOcclusionAutomaticActive = true;
                    surface.GpuOcclusionAutomaticQualifyingFrames = 0;
                    surface.GpuOcclusionAutomaticMinimumFrames = Policy::AutomaticMinimumActiveFrames;
                }
            }
            else
            {
                surface.GpuOcclusionAutomaticQualifyingFrames = 0;
            }
            if (!surface.GpuOcclusionAutomaticActive)
            {
                PublishStandby(surface, requested, GpuOcclusionFallbackReason::BelowAutomaticThreshold);
                return {};
            }
        }

        if (!Policy::BeginAllocationAttempt(surface.GpuOcclusionAllocationRetry, frameIndex))
        {
            if (debugBoundsOnly)
                return prepared;
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::ResourceAllocationFailed) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return prepared;
        }

        try
        {
            if (!commands || Specification.MaximumFramesInFlight == 0)
                throw std::logic_error("GPU occlusion preparation requires an active rendered frame.");
            auto& resources = surface.ActiveWorkset().GpuOcclusion;
            resources.OwnershipValid = false;

            if (!resources.Depth || resources.Pyramid.empty() || resources.Width != resourceExtent.Width ||
                resources.Height != resourceExtent.Height)
            {
                GpuOcclusionFrameResources replacement;
                try
                {
                    SDL_GPUTextureCreateInfo depth{};
                    depth.type = SDL_GPU_TEXTURETYPE_2D;
                    depth.format = ShadowDepthFormat;
                    depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    depth.width = resourceExtent.Width;
                    depth.height = resourceExtent.Height;
                    depth.layer_count_or_depth = 1;
                    depth.num_levels = 1;
                    depth.sample_count = SDL_GPU_SAMPLECOUNT_1;
                    replacement.Depth = SDL_CreateGPUTexture(Device, &depth);
                    if (!replacement.Depth)
                        throw std::runtime_error("SDL_CreateGPUTexture(occlusion depth) failed: " + LastSdlError());
                    const auto depthName = "Occlusion depth - " + surface.Specification.Name + " - frame slot " +
                                           std::to_string(frameIndex);
                    SDL_SetGPUTextureName(Device, replacement.Depth, depthName.c_str());
                    replacement.Width = resourceExtent.Width;
                    replacement.Height = resourceExtent.Height;
                    std::uint32_t levelWidth = (resourceExtent.Width + 1U) / 2U;
                    std::uint32_t levelHeight = (resourceExtent.Height + 1U) / 2U;
                    while (replacement.Pyramid.size() < MaximumGpuOcclusionPyramidLevels)
                    {
                        SDL_GPUTextureCreateInfo level{};
                        level.type = SDL_GPU_TEXTURETYPE_2D;
                        level.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
                        level.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
                        level.width = levelWidth;
                        level.height = levelHeight;
                        level.layer_count_or_depth = 1;
                        level.num_levels = 1;
                        level.sample_count = SDL_GPU_SAMPLECOUNT_1;
                        auto* texture = SDL_CreateGPUTexture(Device, &level);
                        if (!texture)
                        {
                            throw std::runtime_error("SDL_CreateGPUTexture(occlusion pyramid) failed: " +
                                                     LastSdlError());
                        }
                        const auto levelName =
                            "Occlusion depth pyramid mip " + std::to_string(replacement.Pyramid.size()) + " - " +
                            surface.Specification.Name + " - frame slot " + std::to_string(frameIndex);
                        SDL_SetGPUTextureName(Device, texture, levelName.c_str());
                        replacement.Pyramid.push_back(texture);
                        if (levelWidth == 1U && levelHeight == 1U)
                            break;
                        levelWidth = (levelWidth + 1U) / 2U;
                        levelHeight = (levelHeight + 1U) / 2U;
                    }
                }
                catch (const GpuDeviceLostError&)
                {
                    throw;
                }
                catch (const std::exception& error)
                {
                    ThrowIfDeviceLost("GPU occlusion frame-resource creation", error.what());
                    ReleaseGpuOcclusionFrameResources(replacement);
                    throw;
                }
                catch (...)
                {
                    ReleaseGpuOcclusionFrameResources(replacement);
                    throw;
                }
                ReleaseGpuOcclusionFrameResources(resources);
                resources = std::move(replacement);
            }

            const auto ensureBuffer = [this](GpuOcclusionBuffer& buffer, const std::uint64_t bytes,
                                             const SDL_GPUBufferUsageFlags usage, const char* diagnostic)
            {
                if (buffer.Buffer && buffer.CapacityBytes >= bytes)
                    return;
                SDL_GPUBufferCreateInfo information{};
                information.usage = usage;
                information.size = GrowCapacity(bytes);
                auto* replacement = SDL_CreateGPUBuffer(Device, &information);
                if (!replacement)
                    throw std::runtime_error(std::string("SDL_CreateGPUBuffer(") + diagnostic +
                                             ") failed: " + LastSdlError());
                Retire(std::exchange(buffer.Buffer, replacement));
                SDL_SetGPUBufferName(Device, replacement, diagnostic);
                buffer.CapacityBytes = information.size;
                ++Statistics.DynamicUploadBufferReallocations;
            };
            const auto candidateBytes = static_cast<std::uint64_t>(candidates.size()) * sizeof(candidates.front());
            const auto instanceBytes =
                static_cast<std::uint64_t>(inputInstances.size()) * sizeof(inputInstances.front());
            const auto geometryUintBytes = static_cast<std::uint64_t>(prepared.CandidateCount) * sizeof(std::uint32_t);
            const auto chunkBytes = static_cast<std::uint64_t>(chunks.size()) * sizeof(chunks.front());
            const auto batchBytes = static_cast<std::uint64_t>(batches.size()) * sizeof(batches.front());
            const auto chunkUintBytes = static_cast<std::uint64_t>(chunks.size()) * sizeof(std::uint32_t);
            const auto indirectBytes = static_cast<std::uint64_t>(indirect.size()) * sizeof(indirect.front());
            ensureBuffer(resources.Candidates, candidateBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                             SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                         "occlusion candidates");
            ensureBuffer(resources.InputInstances, instanceBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                             SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                         "occlusion input instances");
            ensureBuffer(resources.GeometryVisibility, geometryUintBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                             SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                         "geometry visibility");
            const auto ensureConservativeMask =
                [&](GpuOcclusionBuffer& buffer, const std::uint32_t count, const char* diagnostic)
            {
                ensureBuffer(buffer, static_cast<std::uint64_t>(std::max(count, 1U)) * sizeof(std::uint32_t),
                             SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                                 SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                             diagnostic);
            };
            ensureConservativeMask(resources.VfxVisibilityMask, vfxVisibilityCandidates, "VFX visibility mask");
            ensureConservativeMask(resources.LocalLightVisibilityMask, localLightCandidates,
                                   "local-light visibility mask");
            ensureConservativeMask(resources.SpatialVolumeVisibilityMask, spatialVolumeCandidates,
                                   "spatial-volume visibility mask");
            ensureBuffer(resources.LocalOffsets, geometryUintBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
                         "occlusion local offsets");
            ensureBuffer(resources.Chunks, chunkBytes, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, "occlusion chunks");
            ensureBuffer(resources.ChunkCounts, chunkUintBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
                         "occlusion chunk counts");
            ensureBuffer(resources.Batches, batchBytes, SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ, "occlusion batches");
            ensureBuffer(resources.ChunkOffsets, chunkUintBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
                         "occlusion chunk offsets");
            ensureBuffer(resources.VisibleInstances, instanceBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                         "occlusion visible instances");
            ensureBuffer(resources.IndirectArguments, indirectBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_INDIRECT,
                         "occlusion indirect arguments");
            ensureBuffer(resources.Status, sizeof(GpuOcclusionStatus), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
                         "occlusion status");

            struct UploadRegion final
            {
                SDL_GPUBuffer* Destination = nullptr;
                std::uint32_t SourceOffset = 0;
                std::uint32_t Size = 0;
            };
            std::vector<std::byte> upload;
            std::vector<UploadRegion> regions;
            const auto append = [&](const auto& values, SDL_GPUBuffer* destination)
            {
                const auto bytes = std::as_bytes(std::span(values));
                const auto aligned = AlignUpload(upload.size());
                upload.resize(aligned + bytes.size());
                std::memcpy(upload.data() + aligned, bytes.data(), bytes.size());
                regions.push_back(
                    {destination, static_cast<std::uint32_t>(aligned), static_cast<std::uint32_t>(bytes.size())});
            };
            append(candidates, resources.Candidates.Buffer);
            append(inputInstances, resources.InputInstances.Buffer);
            append(chunks, resources.Chunks.Buffer);
            append(batches, resources.Batches.Buffer);
            append(indirect, resources.IndirectArguments.Buffer);
            const auto appendConservativeMask = [&](const std::uint32_t count, SDL_GPUBuffer* destination)
            {
                if (count == 0 || !destination)
                    return;
                std::vector<std::uint32_t> visible(count, 1U);
                append(visible, destination);
            };
            if (debugBoundsOnly)
                appendConservativeMask(prepared.CandidateCount, resources.GeometryVisibility.Buffer);
            appendConservativeMask(vfxVisibilityCandidates, resources.VfxVisibilityMask.Buffer);
            appendConservativeMask(localLightCandidates, resources.LocalLightVisibilityMask.Buffer);
            appendConservativeMask(spatialVolumeCandidates, resources.SpatialVolumeVisibilityMask.Buffer);
            const auto classifiedConsumerCount = [&](const GpuVisibilityConsumer consumer)
            {
                return static_cast<std::uint32_t>(
                    std::ranges::count_if(candidates, [consumer](const GpuOcclusionCandidate& candidate)
                                          { return candidate.Metadata[3] == static_cast<std::uint32_t>(consumer); }));
            };
            GpuOcclusionStatus initialStatus{};
            const auto vfxClassified = classifiedConsumerCount(GpuVisibilityConsumer::VfxVisibilityMask);
            const auto localLightClassified = classifiedConsumerCount(GpuVisibilityConsumer::ForwardPlusLightMask);
            const auto spatialClassified = classifiedConsumerCount(GpuVisibilityConsumer::SpatialVolumeMask);
            initialStatus.ConsumerVisible[static_cast<std::size_t>(GpuVisibilityConsumer::VfxVisibilityMask)] =
                vfxVisibilityCandidates - std::min(vfxVisibilityCandidates, vfxClassified);
            initialStatus.ConsumerVisible[static_cast<std::size_t>(GpuVisibilityConsumer::ForwardPlusLightMask)] =
                localLightCandidates - std::min(localLightCandidates, localLightClassified);
            initialStatus.ConsumerVisible[static_cast<std::size_t>(GpuVisibilityConsumer::SpatialVolumeMask)] =
                spatialVolumeCandidates - std::min(spatialVolumeCandidates, spatialClassified);
            const std::array initialStatuses{initialStatus};
            append(initialStatuses, resources.Status.Buffer);

            const auto uploadCapacity = GrowCapacity(upload.size());
            if (!resources.Upload || resources.UploadCapacityBytes < uploadCapacity)
            {
                SDL_GPUTransferBufferCreateInfo information{};
                information.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                information.size = uploadCapacity;
                auto* replacement = SDL_CreateGPUTransferBuffer(Device, &information);
                if (!replacement)
                    throw std::runtime_error("SDL_CreateGPUTransferBuffer(occlusion upload) failed: " + LastSdlError());
                Retire(std::exchange(resources.Upload, replacement));
                resources.UploadCapacityBytes = uploadCapacity;
            }
            if (!resources.Readback)
            {
                SDL_GPUTransferBufferCreateInfo information{};
                information.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                information.size = sizeof(GpuOcclusionStatus);
                resources.Readback = SDL_CreateGPUTransferBuffer(Device, &information);
                if (!resources.Readback)
                    throw std::runtime_error("SDL_CreateGPUTransferBuffer(occlusion readback) failed: " +
                                             LastSdlError());
            }
            auto* mapped = SDL_MapGPUTransferBuffer(Device, resources.Upload, true);
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(occlusion upload) failed: " + LastSdlError());
            std::memcpy(mapped, upload.data(), upload.size());
            SDL_UnmapGPUTransferBuffer(Device, resources.Upload);
            auto* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
                throw std::runtime_error("SDL_BeginGPUCopyPass(occlusion upload) failed: " + LastSdlError());
            for (const auto& region : regions)
            {
                const SDL_GPUTransferBufferLocation source{resources.Upload, region.SourceOffset};
                const SDL_GPUBufferRegion destination{region.Destination, 0, region.Size};
                SDL_UploadToGPUBuffer(copy, &source, &destination, true);
            }
            SDL_EndGPUCopyPass(copy);
            Statistics.DynamicUploadBytes += upload.size();

            if (!debugBoundsOnly)
            {
                for (std::size_t index = 0; index < preparedBatches.size(); ++index)
                {
                    auto& sceneBatch = *preparedBatches[index];
                    sceneBatch.GpuOcclusion = true;
                    sceneBatch.GpuOcclusionInstanceBase = batches[index].OutputFirst;
                    sceneBatch.GpuOcclusionIndirectOffset = batches[index].IndirectByteOffset;
                }
                draws.Opaque.GpuOcclusionVisibleInstances = resources.VisibleInstances.Buffer;
                draws.Opaque.GpuOcclusionIndirectArguments = resources.IndirectArguments.Buffer;
                draws.Transparent.GpuOcclusionVisibleInstances = resources.VisibleInstances.Buffer;
                draws.Transparent.GpuOcclusionIndirectArguments = resources.IndirectArguments.Buffer;
            }
            resources.FrameId = packet.AcceptedFrameId;
            resources.FrameSlot = frameSlot;
            resources.SurfaceEpoch = surface.Epoch;
            resources.DeviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
            resources.GeometryVisibilityCount = prepared.CandidateCount;
            resources.VfxVisibilityCount = vfxVisibilityCandidates;
            resources.LocalLightVisibilityCount = localLightCandidates;
            resources.SpatialVolumeVisibilityCount = spatialVolumeCandidates;
            resources.OwnershipValid = true;
            prepared.Resources = std::addressof(resources);
            prepared.DebugBoundsPrepared = visibilityBoundsRequested;
            prepared.Enabled = !debugBoundsOnly;
            if (debugBoundsOnly)
            {
                Policy::RegisterAllocationSuccess(surface.GpuOcclusionAllocationRetry, frameIndex);
                return prepared;
            }
            const auto partialFallbackReason = oversizedBatch    ? GpuOcclusionFallbackReason::OversizedBatch
                                               : legacyShaderAbi ? GpuOcclusionFallbackReason::LegacyShaderAbi
                                                                 : GpuOcclusionFallbackReason::None;
            const bool partialFallbackTransition = partialFallbackReason != GpuOcclusionFallbackReason::None &&
                                                   (diagnostics.State != GpuOcclusionSurfaceState::Active ||
                                                    diagnostics.FallbackReason != partialFallbackReason);
            diagnostics.EffectiveMode = effective;
            diagnostics.State = GpuOcclusionSurfaceState::Active;
            diagnostics.FallbackReason = partialFallbackReason;
            diagnostics.PyramidMipCount = static_cast<std::uint32_t>(resources.Pyramid.size());
            diagnostics.PyramidValid = false;
            Statistics.GpuOcclusionSafeOccluders += diagnostics.SafeOccluders;
            Statistics.GpuOcclusionPyramidMipCount =
                std::max(Statistics.GpuOcclusionPyramidMipCount, diagnostics.PyramidMipCount);
            Statistics.GpuOcclusionEnabled = true;
            if (partialFallbackReason != GpuOcclusionFallbackReason::None)
            {
                Statistics.GpuOcclusionFallbackActive = true;
                Statistics.GpuOcclusionFallbacks += partialFallbackTransition ? 1U : 0U;
            }
            Policy::RegisterAllocationSuccess(surface.GpuOcclusionAllocationRetry, frameIndex);
            return prepared;
        }
        catch (const GpuDeviceLostError&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("GPU occlusion resource preparation", error.what());
            for (auto* batch : preparedBatches)
            {
                batch->GpuOcclusion = false;
                batch->GpuOcclusionInstanceBase = 0;
                batch->GpuOcclusionIndirectOffset = 0;
            }
            draws.Opaque.GpuOcclusionVisibleInstances = nullptr;
            draws.Opaque.GpuOcclusionIndirectArguments = nullptr;
            draws.Transparent.GpuOcclusionVisibleInstances = nullptr;
            draws.Transparent.GpuOcclusionIndirectArguments = nullptr;
            if (debugBoundsOnly)
            {
                if (Policy::RegisterAllocationFailure(surface.GpuOcclusionAllocationRetry, frameIndex))
                {
                    KEIRE_CORE_WARN("GPU occlusion bounds debug resources failed for surface '{}': {}",
                                    surface.Specification.Name, error.what());
                }
                return {};
            }
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::ResourceAllocationFailed) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            surface.GpuOcclusionAutomaticActive = false;
            surface.GpuOcclusionAutomaticQualifyingFrames = 0U;
            surface.GpuOcclusionAutomaticMinimumFrames = 0U;
            if (Policy::RegisterAllocationFailure(surface.GpuOcclusionAllocationRetry, frameIndex))
            {
                const auto retryFrames = surface.GpuOcclusionAllocationRetry.Slots[frameIndex].FramesRemaining;
                KEIRE_CORE_WARN(
                    "GPU occlusion resources failed for surface '{}'; retaining direct draws and retrying after {} "
                    "qualifying frames: {}",
                    surface.Specification.Name, retryFrames, error.what());
            }
            return {};
        }
    }

    void RenderSharedState::RecordGpuOcclusionDepth(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface,
                                                    const SceneRenderPacket& packet,
                                                    const PreparedSceneDrawLists& draws,
                                                    PreparedGpuOcclusion& occlusion)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion depth record");
        if (!occlusion.Enabled || !occlusion.Resources ||
            !VisibilityResourcesOwnedBy(*occlusion.Resources, surface, packet,
                                        DeviceGeneration.load(std::memory_order_acquire)))
            return;
        const auto started = std::chrono::steady_clock::now();
        const ScopedGpuDebugGroup debugGroup(commands, "Occlusion depth");
        SDL_InsertGPUDebugLabel(commands, "Occlusion depth safe occluders");
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = occlusion.Resources->Depth;
        depth.clear_depth = 1.0F;
        depth.load_op = SDL_GPU_LOADOP_CLEAR;
        depth.store_op = SDL_GPU_STOREOP_STORE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        auto* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(occlusion depth) failed: " + LastSdlError());
        SDL_GPUBuffer* boundVertices = nullptr;
        SDL_GPUBuffer* boundIndices = nullptr;
        SDL_GPUGraphicsPipeline* boundPipeline = nullptr;
        const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        for (const auto& occluder : occlusion.Occluders)
        {
            const auto& batch = draws.Opaque.Batches[occluder.SceneBatchIndex];
            const auto& draw = draws.Opaque.Draws[batch.First];
            const auto& mesh = ResolveMesh(draw.Item->Mesh);
            auto* pipeline = GpuOcclusionDepthPipelines[OcclusionCullPipelineIndex(occluder.CullMode)];
            if (pipeline != boundPipeline)
            {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                boundPipeline = pipeline;
            }
            auto* vertices = draw.Item->SkinnedAssetVertices ? draw.Item->SkinnedAssetVertices : mesh.AssetVertices;
            if (vertices != boundVertices)
            {
                const SDL_GPUBufferBinding binding{vertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
                boundVertices = vertices;
            }
            if (mesh.Indices != boundIndices)
            {
                const SDL_GPUBufferBinding binding{mesh.Indices, 0};
                SDL_BindGPUIndexBuffer(pass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                boundIndices = mesh.Indices;
            }
            SDL_BindGPUVertexStorageBuffers(pass, 0, &occlusion.Resources->InputInstances.Buffer, 1);
            const GpuOcclusionDepthUniforms uniforms{viewProjection, {occluder.InstanceFirst, 0U, 0U, 0U}};
            SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
            SDL_DrawGPUIndexedPrimitives(pass, draw.Submesh.IndexCount, occluder.InstanceCount, draw.Submesh.FirstIndex,
                                         0, 0);
            if (occluder.InstanceFirst < batch.GpuOcclusionInstanceBase ||
                occluder.InstanceFirst - batch.GpuOcclusionInstanceBase > batch.Count ||
                occluder.InstanceCount > batch.Count - (occluder.InstanceFirst - batch.GpuOcclusionInstanceBase))
            {
                throw std::logic_error("GPU occlusion depth evidence range exceeds its prepared scene batch.");
            }
            const auto batchInstanceFirst = occluder.InstanceFirst - batch.GpuOcclusionInstanceBase;
            for (std::uint32_t instance = 0; instance < occluder.InstanceCount; ++instance)
            {
                const auto& instanceDraw = draws.Opaque.Draws[batch.First + batchInstanceFirst + instance];
                if (ResolvedVisibilityClass(*instanceDraw.Item) == GpuVisibilityClass::SkinnedMesh &&
                    instanceDraw.Item->SkinnedAssetVertices == vertices &&
                    instanceDraw.Item->HasFreshCurrentPoseBounds(packet.FrameIndex, mesh.Submeshes.size()))
                {
                    ++occlusion.FreshPoseSkinnedDepthDraws;
                }
            }
            ++Statistics.DepthDrawCalls;
            const auto triangles = static_cast<std::uint64_t>(draw.Submesh.IndexCount / 3U) * occluder.InstanceCount;
            Statistics.DepthTriangles += triangles;
            Statistics.GpuOcclusionDepthTriangles += triangles;
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
        Statistics.GpuOcclusionDepthPassMilliseconds +=
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    void RenderSharedState::RecordGpuOcclusionPyramid(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                      const SceneRenderPacket& packet,
                                                      const PreparedGpuOcclusion& occlusion)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion HZB record");
        if (!occlusion.Enabled || !occlusion.Resources || occlusion.Resources->Pyramid.empty() ||
            !VisibilityResourcesOwnedBy(*occlusion.Resources, surface, packet,
                                        DeviceGeneration.load(std::memory_order_acquire)))
            return;
        const auto started = std::chrono::steady_clock::now();
        const ScopedGpuDebugGroup debugGroup(commands, "Occlusion depth pyramid");
        std::uint32_t sourceWidth = occlusion.Resources->Width;
        std::uint32_t sourceHeight = occlusion.Resources->Height;
        SDL_GPUTexture* source = occlusion.Resources->Depth;
        for (std::size_t level = 0; level < occlusion.Resources->Pyramid.size(); ++level)
        {
            const auto levelName = "Occlusion HZB mip " + std::to_string(level);
            const ScopedGpuDebugGroup levelDebugGroup(commands, levelName.c_str());
            const std::uint32_t targetWidth = (sourceWidth + 1U) / 2U;
            const std::uint32_t targetHeight = (sourceHeight + 1U) / 2U;
            auto* target = occlusion.Resources->Pyramid[level];
            const SDL_GPUStorageTextureReadWriteBinding write{target, 0, 0, false};
            auto* pass = SDL_BeginGPUComputePass(commands, &write, 1, nullptr, 0);
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(occlusion pyramid) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, level == 0 ? GpuOcclusionBuildBasePipeline : GpuOcclusionReducePipeline);
            const SDL_GPUTextureSamplerBinding sample{source, GpuOcclusionSampler};
            SDL_BindGPUComputeSamplers(pass, 0, &sample, 1);
            const GpuOcclusionPyramidUniforms uniforms{{sourceWidth, sourceHeight, targetWidth, targetHeight}};
            SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
            SDL_DispatchGPUCompute(pass, (targetWidth + 7U) / 8U, (targetHeight + 7U) / 8U, 1);
            SDL_EndGPUComputePass(pass);
            ++Statistics.GpuOcclusionDispatches;
            ++Statistics.Passes;
            source = target;
            sourceWidth = targetWidth;
            sourceHeight = targetHeight;
        }
        Statistics.GpuOcclusionPyramidRecordingMilliseconds +=
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    void RenderSharedState::RecordGpuOcclusionCulling(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                      const SceneRenderPacket& packet, PreparedSceneDrawLists& draws,
                                                      const PreparedGpuOcclusion& occlusion)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion classify scan scatter record");
        if (!occlusion.Enabled || !occlusion.Resources ||
            !VisibilityResourcesOwnedBy(*occlusion.Resources, surface, packet,
                                        DeviceGeneration.load(std::memory_order_acquire)))
        {
            for (auto* drawList : {&draws.Opaque, &draws.Transparent})
            {
                for (auto& batch : drawList->Batches)
                {
                    batch.GpuOcclusion = false;
                    batch.GpuOcclusionInstanceBase = 0;
                    batch.GpuOcclusionIndirectOffset = 0;
                }
                drawList->GpuOcclusionVisibleInstances = nullptr;
                drawList->GpuOcclusionIndirectArguments = nullptr;
            }
            return;
        }
        const auto started = std::chrono::steady_clock::now();
        auto& resources = *occlusion.Resources;
        const ScopedGpuDebugGroup debugGroup(commands, "GPU occlusion culling");
        std::uint32_t recordedDispatches = 0;

        {
            KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion classify");
            const ScopedGpuDebugGroup stageDebugGroup(commands, "Occlusion classify");
            const std::array writes{
                SDL_GPUStorageBufferReadWriteBinding{resources.GeometryVisibility.Buffer, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.VfxVisibilityMask.Buffer, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.LocalLightVisibilityMask.Buffer, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.SpatialVolumeVisibilityMask.Buffer, false},
                SDL_GPUStorageBufferReadWriteBinding{resources.Status.Buffer, false}};
            auto* pass =
                SDL_BeginGPUComputePass(commands, nullptr, 0, writes.data(), static_cast<std::uint32_t>(writes.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(occlusion classify) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, GpuOcclusionClassifyPipeline);
            std::array<SDL_GPUTextureSamplerBinding, MaximumGpuOcclusionPyramidLevels> pyramid{};
            for (std::size_t level = 0; level < pyramid.size(); ++level)
            {
                pyramid[level] = {resources.Pyramid[std::min(level, resources.Pyramid.size() - 1U)],
                                  GpuOcclusionSampler};
            }
            SDL_BindGPUComputeSamplers(pass, 0, pyramid.data(), static_cast<std::uint32_t>(pyramid.size()));
            const std::array read{resources.Candidates.Buffer, resources.InputInstances.Buffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0, read.data(), static_cast<std::uint32_t>(read.size()));
            GpuOcclusionClassifyUniforms uniforms{};
            uniforms.ViewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
            uniforms.ViewportBiasLevels = {static_cast<float>(resources.Width), static_cast<float>(resources.Height),
                                           OcclusionDepthBias, static_cast<float>(resources.Pyramid.size())};
            uniforms.DispatchCounts = {occlusion.ClassificationCandidateCount,
                                       static_cast<std::uint32_t>(resources.Pyramid.size()), 0U, 0U};
            std::uint32_t width = (resources.Width + 1U) / 2U;
            std::uint32_t height = (resources.Height + 1U) / 2U;
            for (std::size_t level = 0; level < resources.Pyramid.size(); ++level)
            {
                uniforms.HierarchySizes[level] = {width, height, 0U, 0U};
                width = (width + 1U) / 2U;
                height = (height + 1U) / 2U;
            }
            SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
            SDL_DispatchGPUCompute(pass, (occlusion.ClassificationCandidateCount + 255U) / 256U, 1, 1);
            ++recordedDispatches;
            SDL_EndGPUComputePass(pass);
        }
        {
            KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion block scan");
            const ScopedGpuDebugGroup stageDebugGroup(commands, "Occlusion block scan");
            const std::array write{SDL_GPUStorageBufferReadWriteBinding{resources.LocalOffsets.Buffer, false},
                                   SDL_GPUStorageBufferReadWriteBinding{resources.ChunkCounts.Buffer, false}};
            auto* pass =
                SDL_BeginGPUComputePass(commands, nullptr, 0, write.data(), static_cast<std::uint32_t>(write.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(occlusion block scan) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, GpuOcclusionScanBlocksPipeline);
            const std::array read{resources.GeometryVisibility.Buffer, resources.Chunks.Buffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0, read.data(), static_cast<std::uint32_t>(read.size()));
            for (std::uint32_t base = 0; base < occlusion.ChunkCount; base += MaximumGpuDispatchGroupsPerDimension)
            {
                const auto count = std::min(MaximumGpuDispatchGroupsPerDimension, occlusion.ChunkCount - base);
                const GpuOcclusionDispatchUniforms uniforms{{occlusion.ChunkCount, occlusion.CandidateCount, base, 0U}};
                SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
                SDL_DispatchGPUCompute(pass, count, 1, 1);
                ++recordedDispatches;
            }
            SDL_EndGPUComputePass(pass);
        }
        {
            KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion batch scan");
            const ScopedGpuDebugGroup stageDebugGroup(commands, "Occlusion batch scan and indirect arguments");
            const std::array write{SDL_GPUStorageBufferReadWriteBinding{resources.ChunkOffsets.Buffer, false},
                                   SDL_GPUStorageBufferReadWriteBinding{resources.IndirectArguments.Buffer, false},
                                   SDL_GPUStorageBufferReadWriteBinding{resources.Status.Buffer, false}};
            auto* pass =
                SDL_BeginGPUComputePass(commands, nullptr, 0, write.data(), static_cast<std::uint32_t>(write.size()));
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(occlusion batch scan) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, GpuOcclusionScanBatchesPipeline);
            const std::array read{resources.Batches.Buffer, resources.ChunkCounts.Buffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0, read.data(), static_cast<std::uint32_t>(read.size()));
            for (std::uint32_t base = 0; base < occlusion.BatchCount; base += MaximumGpuDispatchGroupsPerDimension)
            {
                const auto count = std::min(MaximumGpuDispatchGroupsPerDimension, occlusion.BatchCount - base);
                const GpuOcclusionDispatchUniforms uniforms{{occlusion.BatchCount, occlusion.ChunkCount, base, 0U}};
                SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
                SDL_DispatchGPUCompute(pass, count, 1, 1);
                ++recordedDispatches;
            }
            SDL_EndGPUComputePass(pass);
        }
        {
            KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion scatter");
            const ScopedGpuDebugGroup stageDebugGroup(commands, "Occlusion stable scatter");
            const SDL_GPUStorageBufferReadWriteBinding write{resources.VisibleInstances.Buffer, false};
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &write, 1);
            if (!pass)
                throw std::runtime_error("SDL_BeginGPUComputePass(occlusion scatter) failed: " + LastSdlError());
            SDL_BindGPUComputePipeline(pass, GpuOcclusionScatterPipeline);
            const std::array read{
                resources.GeometryVisibility.Buffer, resources.LocalOffsets.Buffer, resources.Chunks.Buffer,
                resources.ChunkOffsets.Buffer,       resources.Batches.Buffer,      resources.InputInstances.Buffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0, read.data(), static_cast<std::uint32_t>(read.size()));
            for (std::uint32_t base = 0; base < occlusion.ChunkCount; base += MaximumGpuDispatchGroupsPerDimension)
            {
                const auto count = std::min(MaximumGpuDispatchGroupsPerDimension, occlusion.ChunkCount - base);
                const GpuOcclusionDispatchUniforms uniforms{{occlusion.ChunkCount, occlusion.CandidateCount, base, 0U}};
                SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
                SDL_DispatchGPUCompute(pass, count, 1, 1);
                ++recordedDispatches;
            }
            SDL_EndGPUComputePass(pass);
        }
        Statistics.GpuOcclusionDispatches += recordedDispatches;
        Statistics.Passes += 4U;

        {
            KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion status readback");
            const ScopedGpuDebugGroup readbackDebugGroup(commands, "Occlusion fixed status readback");
            auto* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
                throw std::runtime_error("SDL_BeginGPUCopyPass(occlusion readback) failed: " + LastSdlError());
            const SDL_GPUBufferRegion source{resources.Status.Buffer, 0, sizeof(GpuOcclusionStatus)};
            const SDL_GPUTransferBufferLocation destination{resources.Readback, 0};
            SDL_DownloadFromGPUBuffer(copy, &source, &destination);
            SDL_EndGPUCopyPass(copy);
        }
        GpuOcclusionPendingReadback pending;
        pending.Transfer = resources.Readback;
        pending.SurfaceId = surface.Id;
        pending.SurfaceGeneration = surface.Generation;
        pending.SubmissionEpoch = surface.GpuOcclusionSubmissionEpoch;
        pending.SourceFrame = packet.AcceptedFrameId;
        pending.SourceSurfaceEpoch = surface.Epoch;
        pending.SourceFrameSlot = surface.ActiveWorksetSlot;
        pending.SourceDeviceGeneration = resources.DeviceGeneration;
        pending.RequestedMode = packet.Environment.GpuOcclusion;
        pending.Candidates = occlusion.CandidateCount;
        pending.SafeOccluders = surface.GpuOcclusionDiagnostics.SafeOccluders;
        pending.PyramidMipCount = static_cast<std::uint32_t>(resources.Pyramid.size());
        pending.CandidateTriangles = occlusion.CandidateTriangles;
        pending.LocalLightCandidates = resources.LocalLightVisibilityCount;
        pending.FreshPoseSkinnedCandidates = occlusion.FreshPoseSkinnedCandidates;
        pending.FreshPoseSkinnedDepthDraws = occlusion.FreshPoseSkinnedDepthDraws;
        pending.VfxMaskEntries = resources.VfxVisibilityCount;
        pending.SpatialMaskEntries = resources.SpatialVolumeVisibilityCount;
        FrameGpuOcclusionReadbacks.push_back(std::move(pending));
        draws.Opaque.GpuOcclusionVisibleInstances = resources.VisibleInstances.Buffer;
        draws.Opaque.GpuOcclusionIndirectArguments = resources.IndirectArguments.Buffer;
        draws.Transparent.GpuOcclusionVisibleInstances = resources.VisibleInstances.Buffer;
        draws.Transparent.GpuOcclusionIndirectArguments = resources.IndirectArguments.Buffer;
        surface.GpuOcclusionDiagnostics.PyramidValid = true;
        Statistics.GpuOcclusionCullingRecordingMilliseconds +=
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    bool RenderSharedState::RecordForwardPlusVisibilityMask(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                            const SceneRenderPacket& packet,
                                                            const PreparedGpuOcclusion& occlusion)
    {
        if (!commands || !ForwardPlusVisibilityPipeline || !occlusion.Enabled || !occlusion.Resources)
            return false;

        const auto deviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
        const auto& visibility = *occlusion.Resources;
        auto& forwardPlus = surface.ActiveWorkset().ForwardPlus;
        if (!VisibilityResourcesOwnedBy(visibility, surface, packet, deviceGeneration) ||
            !forwardPlus.OwnedBy(packet.AcceptedFrameId, surface.ActiveWorksetSlot, surface.Epoch, deviceGeneration) ||
            !visibility.LocalLightVisibilityMask.Buffer ||
            visibility.LocalLightVisibilityCount != packet.LocalLights.size() || packet.LocalLights.empty() ||
            forwardPlus.Empty())
        {
            return false;
        }

        const auto tileCount = static_cast<std::uint64_t>(forwardPlus.Columns) * forwardPlus.Rows;
        if (tileCount == 0U || tileCount > static_cast<std::uint64_t>(MaximumGpuDispatchGroupsPerDimension) * 64U)
            return false;

        KEIRE_TELEMETRY_ZONE_SCOPED("Forward+ local-light visibility mask consume");
        const ScopedGpuDebugGroup debugGroup(commands, "Forward+ visibility compaction");
        const std::array writes{SDL_GPUStorageBufferReadWriteBinding{forwardPlus.Tiles, false},
                                SDL_GPUStorageBufferReadWriteBinding{forwardPlus.LightIndices, false}};
        auto* pass =
            SDL_BeginGPUComputePass(commands, nullptr, 0, writes.data(), static_cast<std::uint32_t>(writes.size()));
        if (!pass)
            throw std::runtime_error("SDL_BeginGPUComputePass(Forward+ visibility) failed: " + LastSdlError());
        SDL_BindGPUComputePipeline(pass, ForwardPlusVisibilityPipeline);
        const std::array read{visibility.LocalLightVisibilityMask.Buffer};
        SDL_BindGPUComputeStorageBuffers(pass, 0, read.data(), static_cast<std::uint32_t>(read.size()));
        const ForwardPlusVisibilityUniforms uniforms{
            {static_cast<std::uint32_t>(tileCount), visibility.LocalLightVisibilityCount, 0U, 0U}};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        SDL_DispatchGPUCompute(pass, (static_cast<std::uint32_t>(tileCount) + 63U) / 64U, 1, 1);
        SDL_EndGPUComputePass(pass);
        ++Statistics.GpuOcclusionDispatches;
        ++Statistics.Passes;
        return true;
    }

    void RenderSharedState::RecordGpuOcclusionDebug(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                    const SceneRenderPacket& packet,
                                                    const PreparedGpuOcclusion& occlusion)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion debug view record");
        const auto debugMode = surface.GpuOcclusionDebugMode.load(std::memory_order_acquire);
        const bool debugBoundsAvailable =
            debugMode == GpuOcclusionDebugView::VisibilityBounds && occlusion.DebugBoundsPrepared;
        if (!commands || (!occlusion.Enabled && !debugBoundsAvailable) || !occlusion.Resources ||
            debugMode == GpuOcclusionDebugView::None ||
            !VisibilityResourcesOwnedBy(*occlusion.Resources, surface, packet,
                                        DeviceGeneration.load(std::memory_order_acquire)))
        {
            return;
        }
        auto& resources = *occlusion.Resources;
        auto* target = surface.Resources.WriterColor(surface.ActiveWorksetSlot);
        if (!target)
            return;

        if (debugMode == GpuOcclusionDebugView::HierarchicalDepth &&
            (resources.Pyramid.empty() || !GpuOcclusionDebugPyramidPipeline))
        {
            return;
        }
        if (debugMode == GpuOcclusionDebugView::VisibilityBounds &&
            (!GpuOcclusionDebugBoundsPipeline || occlusion.CandidateCount == 0U))
        {
            return;
        }

        const ScopedGpuDebugGroup debugGroup(commands, "GPU occlusion debug view");
        SDL_GPUColorTargetInfo color{};
        color.texture = target;
        color.load_op =
            debugMode == GpuOcclusionDebugView::HierarchicalDepth ? SDL_GPU_LOADOP_DONT_CARE : SDL_GPU_LOADOP_LOAD;
        color.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        if (!pass)
        {
            const auto detail = LastSdlError();
            if (const auto diagnostic = ClassifyDeviceFailure("SDL_BeginGPURenderPass(occlusion debug)", detail))
                throw GpuDeviceLostError(*diagnostic);
            KEIRE_CORE_WARN("Could not begin GPU occlusion debug pass for surface '{}': {}", surface.Specification.Name,
                            detail);
            return;
        }

        if (debugMode == GpuOcclusionDebugView::HierarchicalDepth)
        {
            const auto selectedMip = std::min<std::size_t>(
                surface.GpuOcclusionDebugMipLevel.load(std::memory_order_acquire), resources.Pyramid.size() - 1U);
            surface.GpuOcclusionDebugMipLevel.store(static_cast<std::uint32_t>(selectedMip), std::memory_order_release);
            const SDL_GPUTextureSamplerBinding binding{resources.Pyramid[selectedMip], GpuOcclusionSampler};
            SDL_BindGPUGraphicsPipeline(pass, GpuOcclusionDebugPyramidPipeline);
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }
        else
        {
            const std::array storage{resources.Candidates.Buffer, resources.InputInstances.Buffer,
                                     resources.GeometryVisibility.Buffer};
            const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
            SDL_BindGPUGraphicsPipeline(pass, GpuOcclusionDebugBoundsPipeline);
            SDL_BindGPUVertexStorageBuffers(pass, 0, storage.data(), static_cast<std::uint32_t>(storage.size()));
            SDL_PushGPUVertexUniformData(commands, 0, &viewProjection, sizeof(viewProjection));
            SDL_DrawGPUPrimitives(pass, 24U, occlusion.CandidateCount, 0U, 0U);
        }
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }
} // namespace Keire::RenderBackend
