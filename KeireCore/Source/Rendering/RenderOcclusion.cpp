#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "KeireInternal/Diagnostics/TelemetryInternal.h"
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

    struct ProjectedRectangle final
    {
        float MinimumX = 0.0F;
        float MinimumY = 0.0F;
        float MaximumX = 0.0F;
        float MaximumY = 0.0F;

        [[nodiscard]] float Area() const noexcept
        {
            return std::max(0.0F, MaximumX - MinimumX) * std::max(0.0F, MaximumY - MinimumY);
        }
    };

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

    [[nodiscard]] ProjectedRectangle ProjectedBoundsPixels(const Keire::Matrix4& clipFromLocal,
                                                           const Keire::MeshBounds bounds, const std::uint32_t width,
                                                           const std::uint32_t height) noexcept
    {
        using Keire::RenderBackend::GeometryDetail::TransformClip;
        float minimumX = static_cast<float>(width);
        float minimumY = static_cast<float>(height);
        float maximumX = 0.0F;
        float maximumY = 0.0F;
        for (std::uint32_t corner = 0; corner < 8U; ++corner)
        {
            const Keire::Vector3 point{(corner & 1U) != 0U ? bounds.Maximum.X : bounds.Minimum.X,
                                       (corner & 2U) != 0U ? bounds.Maximum.Y : bounds.Minimum.Y,
                                       (corner & 4U) != 0U ? bounds.Maximum.Z : bounds.Minimum.Z};
            const auto clip = TransformClip(clipFromLocal, point);
            if (!std::isfinite(clip.X) || !std::isfinite(clip.Y) || !std::isfinite(clip.W))
                return {};
            if (clip.W <= 0.00001F)
                return {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)};
            const float x = (clip.X / clip.W * 0.5F + 0.5F) * static_cast<float>(width);
            const float y = (-clip.Y / clip.W * 0.5F + 0.5F) * static_cast<float>(height);
            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
        minimumX = std::clamp(minimumX, 0.0F, static_cast<float>(width));
        minimumY = std::clamp(minimumY, 0.0F, static_cast<float>(height));
        maximumX = std::clamp(maximumX, 0.0F, static_cast<float>(width));
        maximumY = std::clamp(maximumY, 0.0F, static_cast<float>(height));
        return {minimumX, minimumY, maximumX, maximumY};
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
        for (const auto& item : packet.DrawItems)
        {
            if (item.VisibilityClass == GpuVisibilityClass::MeshVfx)
                ++Statistics.GpuOcclusionMeshVfxCandidates;
            else if (item.Skin)
                ++Statistics.GpuOcclusionSkinnedMeshCandidates;
            else
                ++Statistics.GpuOcclusionStaticMeshCandidates;

            if (item.AlwaysVisible || item.Skin)
                ++Statistics.GpuOcclusionForcedVisibleCandidates;
        }
        const auto localLightCandidates = static_cast<std::uint32_t>(packet.LocalLights.size());
        const auto spatialVolumeCandidates =
            static_cast<std::uint32_t>(packet.ReflectionProbes.size() + packet.LightProbeVolumes.size());
        Statistics.GpuOcclusionLocalLightCandidates += localLightCandidates;
        Statistics.GpuOcclusionSpatialVolumeCandidates += spatialVolumeCandidates;
        Statistics.GpuOcclusionForcedVisibleCandidates += localLightCandidates + spatialVolumeCandidates;
        surface.GpuOcclusionDiagnostics.RequestedMode = requested;
        if (requested == GpuOcclusionMode::Disabled)
        {
            (void)PublishFallback(surface, requested, GpuOcclusionFallbackReason::DisabledBySetting);
            surface.GpuOcclusionAutomaticActive = false;
            surface.GpuOcclusionAutomaticQualifyingFrames = 0;
            surface.GpuOcclusionAutomaticMinimumFrames = 0;
            surface.GpuOcclusionAutomaticCooldownFrames = 0;
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
        const auto frameIndex = SkinningOutputSlot(ActiveGpuSubmissionSerial, frameSlotCount);
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
        if (!GpuOcclusionCapability)
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
            if (!material || material->Topology != ShaderPrimitiveTopology::TriangleList || !material->DepthTest ||
                IsTransparentMaterial(material->Surface.AlphaMode))
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
                candidates.push_back(
                    {{instanceDraw.Submesh.Bounds.Minimum.X, instanceDraw.Submesh.Bounds.Minimum.Y,
                      instanceDraw.Submesh.Bounds.Minimum.Z, 0.0F},
                     {instanceDraw.Submesh.Bounds.Maximum.X, instanceDraw.Submesh.Bounds.Maximum.Y,
                      instanceDraw.Submesh.Bounds.Maximum.Z, 0.0F},
                     {(instanceDraw.Item->AlwaysVisible || instanceDraw.Item->Skin) ? ForceVisibleFlag : 0U,
                      static_cast<std::uint32_t>(instanceDraw.Item->Skin ? GpuVisibilityClass::SkinnedMesh
                                                                         : instanceDraw.Item->VisibilityClass),
                      0U, 0U}});
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
                material->Surface.AlphaMode == MaterialAlphaMode::Opaque && material->DepthWrite && !draw.Item->Skin &&
                HasShaderOcclusionSupport(material->OcclusionSupport, ShaderOcclusionSupport::DepthOnlyGeometryMatch);
            if (!depthCompatible)
                continue;
            std::uint32_t rangeFirst = 0;
            std::uint32_t rangeCount = 0;
            const auto flushRange = [&]
            {
                if (rangeCount == 0U)
                    return;
                prepared.Occluders.push_back({static_cast<std::uint32_t>(sceneBatchIndex), rangeFirst, rangeCount,
                                              OcclusionCullMode(*material)});
                rangeCount = 0U;
            };
            for (std::uint32_t instance = 0; instance < sceneBatch.Count; ++instance)
            {
                const auto& instanceDraw = draws.Opaque.Draws[drawIndex + instance];
                const auto clipFromLocal = Math::Multiply(packet.Camera.Projection,
                                                          Math::Multiply(packet.Camera.View, instanceDraw.Item->World));
                const auto rectangle = ProjectedBoundsPixels(clipFromLocal, instanceDraw.Submesh.Bounds,
                                                             resourceExtent.Width, resourceExtent.Height);
                const float area = rectangle.Area();
                const float minimumOccluderPixels = requested == GpuOcclusionMode::Automatic
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
            if (candidates.size() >= MaximumGpuOcclusionCandidates)
            {
                oversizedBatch = true;
                continue;
            }

            const auto candidateFirst = static_cast<std::uint32_t>(candidates.size());
            candidates.push_back(
                {{draw.Submesh.Bounds.Minimum.X, draw.Submesh.Bounds.Minimum.Y, draw.Submesh.Bounds.Minimum.Z, 0.0F},
                 {draw.Submesh.Bounds.Maximum.X, draw.Submesh.Bounds.Maximum.Y, draw.Submesh.Bounds.Maximum.Z, 0.0F},
                 {(draw.Item->AlwaysVisible || draw.Item->Skin) ? ForceVisibleFlag : 0U,
                  static_cast<std::uint32_t>(draw.Item->Skin ? GpuVisibilityClass::SkinnedMesh
                                                             : draw.Item->VisibilityClass),
                  0U, 0U}});
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
        prepared.BatchCount = static_cast<std::uint32_t>(batches.size());
        prepared.ChunkCount = static_cast<std::uint32_t>(chunks.size());
        auto& diagnostics = surface.GpuOcclusionDiagnostics;
        if (!diagnostics.ReadbackValid)
        {
            diagnostics.Candidates = prepared.CandidateCount;
            diagnostics.Visible = 0;
            diagnostics.Culled = 0;
        }
        diagnostics.SafeOccluders = 0;
        for (const auto& occluder : prepared.Occluders)
            diagnostics.SafeOccluders += occluder.InstanceCount;

        if (prepared.CandidateCount == 0)
        {
            const auto reason = oversizedBatch    ? GpuOcclusionFallbackReason::OversizedBatch
                                : legacyShaderAbi ? GpuOcclusionFallbackReason::LegacyShaderAbi
                                                  : GpuOcclusionFallbackReason::NoEligibleCandidates;
            Statistics.GpuOcclusionFallbacks += PublishFallback(surface, requested, reason) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return {};
        }
        if (prepared.Occluders.empty())
        {
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::NoSafeOccluders) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return {};
        }

        if (requested == GpuOcclusionMode::Automatic)
        {
            if (surface.GpuOcclusionAutomaticCooldownFrames > 0U)
            {
                --surface.GpuOcclusionAutomaticCooldownFrames;
                Statistics.GpuOcclusionFallbacks +=
                    PublishFallback(surface, requested, GpuOcclusionFallbackReason::BelowAutomaticThreshold) ? 1U : 0U;
                Statistics.GpuOcclusionFallbackActive = true;
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
                Statistics.GpuOcclusionFallbacks +=
                    PublishFallback(surface, requested, GpuOcclusionFallbackReason::BelowAutomaticThreshold) ? 1U : 0U;
                Statistics.GpuOcclusionFallbackActive = true;
                return {};
            }
        }

        if (!Policy::BeginAllocationAttempt(surface.GpuOcclusionAllocationRetry, frameIndex))
        {
            Statistics.GpuOcclusionFallbacks +=
                PublishFallback(surface, requested, GpuOcclusionFallbackReason::ResourceAllocationFailed) ? 1U : 0U;
            Statistics.GpuOcclusionFallbackActive = true;
            return prepared;
        }

        try
        {
            if (!commands || Specification.MaximumFramesInFlight == 0)
                throw std::logic_error("GPU occlusion preparation requires an active rendered frame.");
            auto& frames = surface.Resources.GpuOcclusionFrames;
            frames.resize(Specification.MaximumFramesInFlight);
            auto& resources = frames[frameIndex];

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
            const auto uintBytes = static_cast<std::uint64_t>(candidates.size()) * sizeof(std::uint32_t);
            const auto chunkBytes = static_cast<std::uint64_t>(chunks.size()) * sizeof(chunks.front());
            const auto batchBytes = static_cast<std::uint64_t>(batches.size()) * sizeof(batches.front());
            const auto chunkUintBytes = static_cast<std::uint64_t>(chunks.size()) * sizeof(std::uint32_t);
            const auto indirectBytes = static_cast<std::uint64_t>(indirect.size()) * sizeof(indirect.front());
            ensureBuffer(resources.Candidates, candidateBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                         "occlusion candidates");
            ensureBuffer(resources.InputInstances, instanceBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                         "occlusion input instances");
            ensureBuffer(resources.Visibility, uintBytes,
                         SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                             SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                         "occlusion visibility");
            ensureBuffer(resources.LocalOffsets, uintBytes,
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
            const std::array zeroStatus{GpuOcclusionStatus{}};
            append(zeroStatus, resources.Status.Buffer);

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
            prepared.Resources = std::addressof(resources);
            prepared.Enabled = true;
            const auto partialFallbackReason = oversizedBatch    ? GpuOcclusionFallbackReason::OversizedBatch
                                               : legacyShaderAbi ? GpuOcclusionFallbackReason::LegacyShaderAbi
                                                                 : GpuOcclusionFallbackReason::None;
            const bool partialFallbackTransition = partialFallbackReason != GpuOcclusionFallbackReason::None &&
                                                   (diagnostics.State != GpuOcclusionSurfaceState::Active ||
                                                    diagnostics.FallbackReason != partialFallbackReason);
            diagnostics.EffectiveMode = requested;
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
        catch (const std::exception& error)
        {
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

    void RenderSharedState::RecordGpuOcclusionDepth(SDL_GPUCommandBuffer* commands, const SceneRenderPacket& packet,
                                                    const PreparedSceneDrawLists& draws,
                                                    const PreparedGpuOcclusion& occlusion)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion depth record");
        if (!occlusion.Enabled || !occlusion.Resources)
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
            if (mesh.AssetVertices != boundVertices)
            {
                const SDL_GPUBufferBinding binding{mesh.AssetVertices, 0};
                SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
                boundVertices = mesh.AssetVertices;
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
                                                      const PreparedGpuOcclusion& occlusion)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion HZB record");
        (void)surface;
        if (!occlusion.Enabled || !occlusion.Resources || occlusion.Resources->Pyramid.empty())
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
        if (!occlusion.Enabled || !occlusion.Resources)
            return;
        const auto started = std::chrono::steady_clock::now();
        auto& resources = *occlusion.Resources;
        const ScopedGpuDebugGroup debugGroup(commands, "GPU occlusion culling");
        std::uint32_t recordedDispatches = 0;

        {
            KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion classify");
            const ScopedGpuDebugGroup stageDebugGroup(commands, "Occlusion classify");
            const SDL_GPUStorageBufferReadWriteBinding write{resources.Visibility.Buffer, false};
            auto* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &write, 1);
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
            uniforms.DispatchCounts = {occlusion.CandidateCount, static_cast<std::uint32_t>(resources.Pyramid.size()),
                                       0U, 0U};
            std::uint32_t width = (resources.Width + 1U) / 2U;
            std::uint32_t height = (resources.Height + 1U) / 2U;
            for (std::size_t level = 0; level < resources.Pyramid.size(); ++level)
            {
                uniforms.HierarchySizes[level] = {width, height, 0U, 0U};
                width = (width + 1U) / 2U;
                height = (height + 1U) / 2U;
            }
            SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
            SDL_DispatchGPUCompute(pass, (occlusion.CandidateCount + 255U) / 256U, 1, 1);
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
            const std::array read{resources.Visibility.Buffer, resources.Chunks.Buffer};
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
            const std::array read{resources.Visibility.Buffer, resources.LocalOffsets.Buffer,
                                  resources.Chunks.Buffer,     resources.ChunkOffsets.Buffer,
                                  resources.Batches.Buffer,    resources.InputInstances.Buffer};
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
        FrameGpuOcclusionReadbacks.push_back(
            {resources.Readback, surface.Id, surface.Generation, surface.GpuOcclusionSubmissionEpoch, Statistics.Frame,
             packet.Environment.GpuOcclusion, occlusion.CandidateCount, surface.GpuOcclusionDiagnostics.SafeOccluders,
             static_cast<std::uint32_t>(resources.Pyramid.size()), occlusion.CandidateTriangles});
        draws.Opaque.GpuOcclusionVisibleInstances = resources.VisibleInstances.Buffer;
        draws.Opaque.GpuOcclusionIndirectArguments = resources.IndirectArguments.Buffer;
        draws.Transparent.GpuOcclusionVisibleInstances = resources.VisibleInstances.Buffer;
        draws.Transparent.GpuOcclusionIndirectArguments = resources.IndirectArguments.Buffer;
        surface.GpuOcclusionDiagnostics.PyramidValid = true;
        Statistics.GpuOcclusionCullingRecordingMilliseconds +=
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }

    void RenderSharedState::RecordGpuOcclusionDebug(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                    const SceneRenderPacket& packet,
                                                    const PreparedGpuOcclusion& occlusion)
    {
        KEIRE_TELEMETRY_ZONE_SCOPED("GPU occlusion debug view record");
        if (!commands || !occlusion.Enabled || !occlusion.Resources ||
            surface.GpuOcclusionDebugMode == GpuOcclusionDebugView::None)
        {
            return;
        }
        auto& resources = *occlusion.Resources;
        auto* target = surface.HasOutput ? surface.Resources.ExchangeColor : surface.Resources.SampledColor;
        if (!target)
            return;

        if (surface.GpuOcclusionDebugMode == GpuOcclusionDebugView::HierarchicalDepth &&
            (resources.Pyramid.empty() || !GpuOcclusionDebugPyramidPipeline))
        {
            return;
        }
        if (surface.GpuOcclusionDebugMode == GpuOcclusionDebugView::VisibilityBounds &&
            (!GpuOcclusionDebugBoundsPipeline || occlusion.CandidateCount == 0U))
        {
            return;
        }

        const ScopedGpuDebugGroup debugGroup(commands, "GPU occlusion debug view");
        SDL_GPUColorTargetInfo color{};
        color.texture = target;
        color.load_op = surface.GpuOcclusionDebugMode == GpuOcclusionDebugView::HierarchicalDepth
                            ? SDL_GPU_LOADOP_DONT_CARE
                            : SDL_GPU_LOADOP_LOAD;
        color.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        if (!pass)
        {
            KEIRE_CORE_WARN("Could not begin GPU occlusion debug pass for surface '{}': {}", surface.Specification.Name,
                            LastSdlError());
            return;
        }

        if (surface.GpuOcclusionDebugMode == GpuOcclusionDebugView::HierarchicalDepth)
        {
            const auto selectedMip =
                std::min<std::size_t>(surface.GpuOcclusionDebugMipLevel, resources.Pyramid.size() - 1U);
            surface.GpuOcclusionDebugMipLevel = static_cast<std::uint32_t>(selectedMip);
            const SDL_GPUTextureSamplerBinding binding{resources.Pyramid[selectedMip], GpuOcclusionSampler};
            SDL_BindGPUGraphicsPipeline(pass, GpuOcclusionDebugPyramidPipeline);
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }
        else
        {
            const std::array storage{resources.Candidates.Buffer, resources.InputInstances.Buffer,
                                     resources.Visibility.Buffer};
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
