#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <algorithm>
#include <compare>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <tuple>
#include <vector>

namespace Keire::RenderBackend
{
    /// The shared GPU occlusion candidate ceiling. VFX reservations are local ranges within this frame-wide bound.
    inline constexpr std::uint32_t MaximumVfxVisibilityCandidates = 262'144U;
    inline constexpr std::uint32_t InvalidVfxVisibilityOffset = std::numeric_limits<std::uint32_t>::max();

    struct VfxVisibilityHandleIdentity final
    {
        std::uint32_t Index = 0;
        std::uint32_t Generation = 0;

        [[nodiscard]] auto operator<=>(const VfxVisibilityHandleIdentity&) const noexcept = default;
    };

    struct CpuVfxVisibilityInput final
    {
        VfxRendererType Renderer = VfxRendererType::Sprite;
    };

    struct GpuVfxVisibilityInput final
    {
        VfxVisibilityHandleIdentity Handle;
        VfxRendererType Renderer = VfxRendererType::Sprite;
        std::uint32_t Capacity = 0;
        /// True only after the caller has produced finite conservative bounds for every requested candidate.
        bool ConservativeBounds = false;
    };

    struct VfxVisibilitySnapshotInput final
    {
        std::span<const CpuVfxVisibilityInput> CpuParticles;
        std::span<const GpuVfxVisibilityInput> GpuEmitters;
    };

    enum class VfxVisibilityPlanSource : std::uint8_t
    {
        CpuParticle,
        GpuEmitter
    };

    enum class VfxVisibilityPlanDisposition : std::uint8_t
    {
        CandidateRange,
        ForceVisible,
        GeometryOwned
    };

    enum class VfxVisibilityPlanReason : std::uint8_t
    {
        None,
        CpuRenderer,
        CpuMeshAlreadyCapturedAsGeometry,
        UnsafeBounds,
        UnsupportedRenderer,
        InvalidRange,
        CandidateLimit
    };

    struct VfxVisibilityPlanEntry final
    {
        std::uint32_t SnapshotIndex = 0;
        VfxVisibilityPlanSource Source = VfxVisibilityPlanSource::CpuParticle;
        /// Original index in the source span. Sorting emitters never loses the capture identity.
        std::uint32_t SourceIndex = 0;
        VfxVisibilityHandleIdentity Handle;
        VfxRendererType Renderer = VfxRendererType::Sprite;
        /// Sprite and Mesh request one bit per possible particle. Ribbon requests one bit for the whole renderer.
        std::uint32_t RequestedCount = 0;
        std::uint32_t First = InvalidVfxVisibilityOffset;
        std::uint32_t Count = 0;
        VfxVisibilityPlanDisposition Disposition = VfxVisibilityPlanDisposition::ForceVisible;
        VfxVisibilityPlanReason Reason = VfxVisibilityPlanReason::None;

        [[nodiscard]] bool operator==(const VfxVisibilityPlanEntry&) const noexcept = default;
    };

    struct VfxVisibilityPlan final
    {
        std::vector<VfxVisibilityPlanEntry> Entries;
        std::uint32_t CandidateCount = 0;
        std::uint32_t ForceVisibleEntries = 0;
        std::uint32_t GeometryOwnedEntries = 0;
        std::uint32_t OverflowedEntries = 0;

        [[nodiscard]] bool operator==(const VfxVisibilityPlan&) const noexcept = default;
    };

    [[nodiscard]] inline VfxVisibilityPlan
    BuildVfxVisibilityPlan(const std::span<const VfxVisibilitySnapshotInput> snapshots,
                           const std::uint32_t maximumCandidates = MaximumVfxVisibilityCandidates)
    {
        VfxVisibilityPlan result;
        const auto reserveEntry = [&result, maximumCandidates](VfxVisibilityPlanEntry entry)
        {
            if (entry.RequestedCount == 0)
            {
                entry.Disposition = VfxVisibilityPlanDisposition::ForceVisible;
                entry.Reason = VfxVisibilityPlanReason::InvalidRange;
                ++result.ForceVisibleEntries;
            }
            else if (entry.RequestedCount > maximumCandidates - result.CandidateCount)
            {
                entry.Disposition = VfxVisibilityPlanDisposition::ForceVisible;
                entry.Reason = VfxVisibilityPlanReason::CandidateLimit;
                ++result.ForceVisibleEntries;
                ++result.OverflowedEntries;
            }
            else
            {
                entry.First = result.CandidateCount;
                entry.Count = entry.RequestedCount;
                entry.Disposition = VfxVisibilityPlanDisposition::CandidateRange;
                entry.Reason = VfxVisibilityPlanReason::None;
                result.CandidateCount += entry.Count;
            }
            result.Entries.push_back(entry);
        };

        for (std::size_t snapshotIndex = 0; snapshotIndex < snapshots.size(); ++snapshotIndex)
        {
            const auto& snapshot = snapshots[snapshotIndex];
            for (std::size_t particleIndex = 0; particleIndex < snapshot.CpuParticles.size(); ++particleIndex)
            {
                const auto renderer = snapshot.CpuParticles[particleIndex].Renderer;
                VfxVisibilityPlanEntry entry{static_cast<std::uint32_t>(snapshotIndex),
                                             VfxVisibilityPlanSource::CpuParticle,
                                             static_cast<std::uint32_t>(particleIndex),
                                             {},
                                             renderer,
                                             1U};
                if (renderer == VfxRendererType::Mesh)
                {
                    entry.Disposition = VfxVisibilityPlanDisposition::GeometryOwned;
                    entry.Reason = VfxVisibilityPlanReason::CpuMeshAlreadyCapturedAsGeometry;
                    ++result.GeometryOwnedEntries;
                }
                else
                {
                    entry.Disposition = VfxVisibilityPlanDisposition::ForceVisible;
                    entry.Reason = renderer <= VfxRendererType::Volumetric
                                       ? VfxVisibilityPlanReason::CpuRenderer
                                       : VfxVisibilityPlanReason::UnsupportedRenderer;
                    ++result.ForceVisibleEntries;
                }
                result.Entries.push_back(entry);
            }

            std::vector<std::size_t> emitterOrder(snapshot.GpuEmitters.size());
            std::iota(emitterOrder.begin(), emitterOrder.end(), std::size_t{});
            std::ranges::sort(emitterOrder,
                              [&snapshot](const std::size_t left, const std::size_t right)
                              {
                                  return std::tie(snapshot.GpuEmitters[left].Handle.Index,
                                                  snapshot.GpuEmitters[left].Handle.Generation, left) <
                                         std::tie(snapshot.GpuEmitters[right].Handle.Index,
                                                  snapshot.GpuEmitters[right].Handle.Generation, right);
                              });
            for (const auto emitterIndex : emitterOrder)
            {
                const auto& emitter = snapshot.GpuEmitters[emitterIndex];
                const bool rendererSupported = emitter.Renderer == VfxRendererType::Sprite ||
                                               emitter.Renderer == VfxRendererType::Mesh ||
                                               emitter.Renderer == VfxRendererType::Ribbon;
                VfxVisibilityPlanEntry entry{static_cast<std::uint32_t>(snapshotIndex),
                                             VfxVisibilityPlanSource::GpuEmitter,
                                             static_cast<std::uint32_t>(emitterIndex),
                                             emitter.Handle,
                                             emitter.Renderer,
                                             emitter.Renderer == VfxRendererType::Ribbon ? 1U : emitter.Capacity};
                if (!rendererSupported)
                {
                    entry.Disposition = VfxVisibilityPlanDisposition::ForceVisible;
                    entry.Reason = VfxVisibilityPlanReason::UnsupportedRenderer;
                    ++result.ForceVisibleEntries;
                    result.Entries.push_back(entry);
                }
                else if (!emitter.ConservativeBounds)
                {
                    entry.Disposition = VfxVisibilityPlanDisposition::ForceVisible;
                    entry.Reason = VfxVisibilityPlanReason::UnsafeBounds;
                    ++result.ForceVisibleEntries;
                    result.Entries.push_back(entry);
                }
                else
                    reserveEntry(entry);
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
