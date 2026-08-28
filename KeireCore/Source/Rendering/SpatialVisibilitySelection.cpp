#include "KeireInternal/Rendering/SpatialVisibilitySelectionInternal.h"

#include "Keire/Math/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        [[nodiscard]] bool InRange(const std::uint32_t value, const std::uint32_t first,
                                   const std::uint32_t count) noexcept
        {
            return value >= first && static_cast<std::uint64_t>(value) < static_cast<std::uint64_t>(first) + count;
        }

        [[nodiscard]] bool ValidReflection(const SpatialReflectionSelectionCandidate& candidate) noexcept
        {
            return candidate.Eligible && candidate.StableId && std::isfinite(candidate.Weight) &&
                   candidate.Weight > 0.0F && std::isfinite(candidate.Distance) && candidate.Distance >= 0.0F &&
                   Math::IsFinite(candidate.Descriptor.WorldToLocal) &&
                   Math::IsFinite(candidate.Descriptor.LocalToWorld) &&
                   Math::IsFinite(candidate.Descriptor.ExtentsWeight) &&
                   Math::IsFinite(candidate.Descriptor.Parameters);
        }

        [[nodiscard]] bool ValidLightProbe(const SpatialLightProbeSelectionCandidate& candidate) noexcept
        {
            return candidate.StableId && candidate.ContainsPoint && candidate.SampleValid &&
                   std::ranges::all_of(candidate.ProbeIrradiance,
                                       [](const Vector4 value) { return Math::IsFinite(value); });
        }

        template <typename Candidate>
        [[nodiscard]] bool Visible(const Candidate& candidate, const SpatialVisibilityMaskView& mask,
                                   const bool maskUsable, bool& usedFailVisible) noexcept
        {
            if (!maskUsable || !candidate.ConservativeBoundsCurrent)
            {
                usedFailVisible = true;
                return true;
            }
            return mask.Values[candidate.FlatVisibilityIndex] != 0U;
        }
    } // namespace

    SpatialVisibilityLayout
    BuildSpatialVisibilityLayout(const std::span<const SpatialVisibilityContributionCounts> contributions)
    {
        SpatialVisibilityLayout result;
        result.Contributions.resize(contributions.size());
        std::uint64_t reflectionCount = 0;
        std::uint64_t lightProbeCount = 0;
        for (const auto contribution : contributions)
        {
            reflectionCount += contribution.ReflectionProbes;
            lightProbeCount += contribution.LightProbeVolumes;
        }
        if (reflectionCount + lightProbeCount > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Spatial visibility layout exceeds the 32-bit GPU index range.");

        result.ReflectionProbeCount = static_cast<std::uint32_t>(reflectionCount);
        result.LightProbeVolumeCount = static_cast<std::uint32_t>(lightProbeCount);
        std::uint32_t reflectionFirst = 0;
        std::uint32_t lightProbeFirst = result.ReflectionProbeCount;
        for (std::size_t index = 0; index < contributions.size(); ++index)
        {
            const auto contribution = contributions[index];
            result.Contributions[index] = {reflectionFirst, contribution.ReflectionProbes, lightProbeFirst,
                                           contribution.LightProbeVolumes};
            reflectionFirst += contribution.ReflectionProbes;
            lightProbeFirst += contribution.LightProbeVolumes;
        }
        return result;
    }

    std::uint32_t FlatReflectionProbeIndex(const SpatialVisibilityLayout& layout, const std::uint32_t contribution,
                                           const std::uint32_t localIndex)
    {
        if (contribution >= layout.Contributions.size() ||
            localIndex >= layout.Contributions[contribution].ReflectionProbeCount)
            throw std::out_of_range("Reflection-probe visibility index is outside its contribution range.");
        return layout.Contributions[contribution].ReflectionProbeFirst + localIndex;
    }

    std::uint32_t FlatLightProbeVolumeIndex(const SpatialVisibilityLayout& layout, const std::uint32_t contribution,
                                            const std::uint32_t localIndex)
    {
        if (contribution >= layout.Contributions.size() ||
            localIndex >= layout.Contributions[contribution].LightProbeVolumeCount)
            throw std::out_of_range("Light-probe-volume visibility index is outside its contribution range.");
        return layout.Contributions[contribution].LightProbeVolumeFirst + localIndex;
    }

    bool CanConsumeSpatialVisibilityMask(const SpatialVisibilityMaskView& mask,
                                         const SpatialVisibilityOwnership& expected,
                                         const std::uint32_t expectedCount) noexcept
    {
        return mask.Ownership.OwnedBy(expected) && mask.Values.size() == expectedCount &&
               std::ranges::all_of(mask.Values, [](const std::uint32_t value) { return value <= 1U; });
    }

    SpatialDrawSelectionResult SelectSpatialLightingForDraw(const SpatialDrawSelectionInput& input)
    {
        SpatialDrawSelectionResult result;
        result.MaskUsable = CanConsumeSpatialVisibilityMask(input.VisibilityMask, input.ExpectedOwnership,
                                                            input.ExpectedVisibilityCount);
        if (result.MaskUsable)
        {
            result.MaskUsable = std::ranges::all_of(input.ReflectionProbes,
                                                    [&](const auto& candidate)
                                                    {
                                                        return !ValidReflection(candidate) ||
                                                               InRange(candidate.FlatVisibilityIndex,
                                                                       input.Contribution.ReflectionProbeFirst,
                                                                       input.Contribution.ReflectionProbeCount);
                                                    }) &&
                                std::ranges::all_of(input.LightProbeVolumes,
                                                    [&](const auto& candidate)
                                                    {
                                                        return !ValidLightProbe(candidate) ||
                                                               InRange(candidate.FlatVisibilityIndex,
                                                                       input.Contribution.LightProbeVolumeFirst,
                                                                       input.Contribution.LightProbeVolumeCount);
                                                    });
        }
        result.UsedFailVisible = !result.MaskUsable;

        std::vector<const SpatialReflectionSelectionCandidate*> reflections;
        reflections.reserve(input.ReflectionProbes.size());
        for (const auto& candidate : input.ReflectionProbes)
        {
            if (!ValidReflection(candidate) ||
                !Visible(candidate, input.VisibilityMask, result.MaskUsable, result.UsedFailVisible))
                continue;
            reflections.push_back(&candidate);
        }
        std::ranges::sort(reflections,
                          [](const auto* left, const auto* right)
                          {
                              if (left->Importance != right->Importance)
                                  return left->Importance > right->Importance;
                              if (left->Weight != right->Weight)
                                  return left->Weight > right->Weight;
                              if (left->Distance != right->Distance)
                                  return left->Distance < right->Distance;
                              if (left->StableId != right->StableId)
                                  return left->StableId < right->StableId;
                              return left->FlatVisibilityIndex < right->FlatVisibilityIndex;
                          });
        if (!reflections.empty())
        {
            const auto importance = reflections.front()->Importance;
            const auto end = std::ranges::find_if(reflections, [&](const auto* candidate)
                                                  { return candidate->Importance != importance; });
            reflections.erase(end, reflections.end());
            if (reflections.size() > 2U)
                reflections.resize(2U);
            float totalWeight = 0.0F;
            for (const auto* candidate : reflections)
                totalWeight += candidate->Weight;
            for (std::size_t index = 0; index < reflections.size(); ++index)
            {
                result.Record.ReflectionProbes[index] = reflections[index]->Descriptor;
                result.Record.ReflectionProbes[index].ExtentsWeight.W = reflections[index]->Weight / totalWeight;
                result.Record.Metadata[index + 1U] = reflections[index]->FlatVisibilityIndex;
                result.Record.Metadata[0] |=
                    index == 0U ? AssetSpatialSelectionHasReflectionProbe0 : AssetSpatialSelectionHasReflectionProbe1;
            }
        }

        std::vector<const SpatialLightProbeSelectionCandidate*> lightProbes;
        lightProbes.reserve(input.LightProbeVolumes.size());
        for (const auto& candidate : input.LightProbeVolumes)
        {
            if (!ValidLightProbe(candidate) ||
                !Visible(candidate, input.VisibilityMask, result.MaskUsable, result.UsedFailVisible))
                continue;
            lightProbes.push_back(&candidate);
        }
        std::ranges::sort(lightProbes,
                          [](const auto* left, const auto* right)
                          {
                              if (left->Priority != right->Priority)
                                  return left->Priority > right->Priority;
                              if (left->StableId != right->StableId)
                                  return left->StableId < right->StableId;
                              return left->FlatVisibilityIndex < right->FlatVisibilityIndex;
                          });
        if (!lightProbes.empty())
        {
            result.Record.ProbeIrradiance = lightProbes.front()->ProbeIrradiance;
            result.Record.Metadata[0] |= AssetSpatialSelectionHasLightProbe;
            result.Record.Metadata[3] = lightProbes.front()->FlatVisibilityIndex;
        }
        if (result.UsedFailVisible)
            result.Record.Metadata[0] |= AssetSpatialSelectionUsedFailVisible;
        return result;
    }
} // namespace Keire::RenderBackend
