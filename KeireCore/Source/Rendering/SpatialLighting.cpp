#include "KeireInternal/Rendering/SpatialLightingInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] float Length(const Vector3 value) noexcept
        {
            return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        }

        [[nodiscard]] Vector3 Normalize(const Vector3 value) noexcept
        {
            const auto length = Length(value);
            return length > 1.0e-8F ? Vector3{value.X / length, value.Y / length, value.Z / length} : Vector3{};
        }

        [[nodiscard]] Vector3 Subtract(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
        }

        [[nodiscard]] float ProbeWeight(const Vector3 worldPosition, const SpatialReflectionProbe& probe) noexcept
        {
            const auto local = Math::TransformPoint(probe.WorldToLocal, worldPosition);
            const auto distance =
                Vector3{probe.BoxExtents.X - std::abs(local.X), probe.BoxExtents.Y - std::abs(local.Y),
                        probe.BoxExtents.Z - std::abs(local.Z)};
            const auto nearest = std::min({distance.X, distance.Y, distance.Z});
            if (nearest < 0.0F)
                return 0.0F;
            return probe.BlendDistance <= 1.0e-6F ? 1.0F : std::clamp(nearest / probe.BlendDistance, 0.0F, 1.0F);
        }

        [[nodiscard]] bool Fits(const std::vector<bool>& occupied, const std::uint16_t gridSize, const std::uint16_t x,
                                const std::uint16_t y, const std::uint16_t tile) noexcept
        {
            if (x + tile > gridSize || y + tile > gridSize)
                return false;
            for (std::uint16_t row = y; row < y + tile; ++row)
                for (std::uint16_t column = x; column < x + tile; ++column)
                    if (occupied[static_cast<std::size_t>(row) * gridSize + column])
                        return false;
            return true;
        }

        void Occupy(std::vector<bool>& occupied, const std::uint16_t gridSize, const std::uint16_t x,
                    const std::uint16_t y, const std::uint16_t tile)
        {
            for (std::uint16_t row = y; row < y + tile; ++row)
                for (std::uint16_t column = x; column < x + tile; ++column)
                    occupied[static_cast<std::size_t>(row) * gridSize + column] = true;
        }
    } // namespace

    std::vector<SelectedReflectionProbe> SelectReflectionProbes(const Vector3 worldPosition,
                                                                const std::span<const SpatialReflectionProbe> probes,
                                                                const std::size_t maximumCount)
    {
        if (!Math::IsFinite(worldPosition) || maximumCount > 2U)
            throw std::invalid_argument("Reflection probe selection accepts a finite point and at most two probes.");
        struct Candidate
        {
            const SpatialReflectionProbe* Probe;
            float Weight;
            float Distance;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(probes.size());
        for (const auto& probe : probes)
        {
            if (!probe.Entity || !Math::IsFinite(probe.LocalToWorld) || !Math::IsFinite(probe.WorldToLocal) ||
                !Math::IsFinite(probe.BoxExtents) || probe.BoxExtents.X <= 0.0F || probe.BoxExtents.Y <= 0.0F ||
                probe.BoxExtents.Z <= 0.0F || !std::isfinite(probe.BlendDistance) || probe.BlendDistance < 0.0F)
                continue;
            const auto weight = ProbeWeight(worldPosition, probe);
            if (weight <= 0.0F)
                continue;
            const auto center = Math::TransformPoint(probe.LocalToWorld, {});
            candidates.push_back({&probe, weight, Length(Subtract(worldPosition, center))});
        }
        std::ranges::sort(candidates,
                          [](const Candidate& left, const Candidate& right)
                          {
                              if (left.Probe->Importance != right.Probe->Importance)
                                  return left.Probe->Importance > right.Probe->Importance;
                              if (left.Weight != right.Weight)
                                  return left.Weight > right.Weight;
                              if (left.Distance != right.Distance)
                                  return left.Distance < right.Distance;
                              return left.Probe->Entity < right.Probe->Entity;
                          });
        if (candidates.empty() || maximumCount == 0U)
            return {};
        const auto selectedImportance = candidates.front().Probe->Importance;
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const Candidate& candidate)
                                        { return candidate.Probe->Importance != selectedImportance; }),
                         candidates.end());
        const auto count = std::min(maximumCount, candidates.size());
        float totalWeight = 0.0F;
        for (std::size_t index = 0; index < count; ++index)
            totalWeight += candidates[index].Weight;
        std::vector<SelectedReflectionProbe> result;
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
            result.push_back({candidates[index].Probe, candidates[index].Weight / totalWeight});
        return result;
    }

    Vector3 BoxProjectedReflection(const Vector3 worldPosition, const Vector3 worldDirection,
                                   const SpatialReflectionProbe& probe) noexcept
    {
        const auto direction = Normalize(worldDirection);
        if (!probe.BoxProjection || Length(direction) <= 1.0e-8F)
            return direction;
        const auto localPosition = Math::TransformPoint(probe.WorldToLocal, worldPosition);
        const auto localDirection = Normalize(Math::TransformDirection(probe.WorldToLocal, direction));
        if (std::abs(localPosition.X) > probe.BoxExtents.X || std::abs(localPosition.Y) > probe.BoxExtents.Y ||
            std::abs(localPosition.Z) > probe.BoxExtents.Z)
            return direction;
        float distance = std::numeric_limits<float>::max();
        const std::array positions{localPosition.X, localPosition.Y, localPosition.Z};
        const std::array directions{localDirection.X, localDirection.Y, localDirection.Z};
        const std::array extents{probe.BoxExtents.X, probe.BoxExtents.Y, probe.BoxExtents.Z};
        for (std::size_t axis = 0; axis < 3U; ++axis)
        {
            if (std::abs(directions[axis]) <= 1.0e-8F)
                continue;
            const auto face = std::copysign(extents[axis], directions[axis]);
            const auto intersection = (face - positions[axis]) / directions[axis];
            if (intersection >= 0.0F)
                distance = std::min(distance, intersection);
        }
        if (!std::isfinite(distance) || distance == std::numeric_limits<float>::max())
            return direction;
        const auto localHit =
            Vector3{localPosition.X + localDirection.X * distance, localPosition.Y + localDirection.Y * distance,
                    localPosition.Z + localDirection.Z * distance};
        const auto worldHit = Math::TransformPoint(probe.LocalToWorld, localHit);
        const auto worldCenter = Math::TransformPoint(probe.LocalToWorld, {});
        return Normalize(Subtract(worldHit, worldCenter));
    }

    std::optional<std::array<Vector3, 9>> SampleLightProbeCoefficients(const LightProbeVolumeDefinition& volume,
                                                                       const Vector3 localPosition)
    {
        if (volume.CountX == 0U || volume.CountY == 0U || volume.CountZ == 0U ||
            volume.Probes.size() != static_cast<std::uint64_t>(volume.CountX) * volume.CountY * volume.CountZ ||
            !Math::IsFinite(localPosition) || !Math::IsFinite(volume.Origin) || !Math::IsFinite(volume.Spacing) ||
            volume.Spacing.X <= 0.0F || volume.Spacing.Y <= 0.0F || volume.Spacing.Z <= 0.0F)
            return std::nullopt;
        const auto grid = Vector3{(localPosition.X - volume.Origin.X) / volume.Spacing.X,
                                  (localPosition.Y - volume.Origin.Y) / volume.Spacing.Y,
                                  (localPosition.Z - volume.Origin.Z) / volume.Spacing.Z};
        if (grid.X < 0.0F || grid.Y < 0.0F || grid.Z < 0.0F || grid.X > static_cast<float>(volume.CountX - 1U) ||
            grid.Y > static_cast<float>(volume.CountY - 1U) || grid.Z > static_cast<float>(volume.CountZ - 1U))
            return std::nullopt;
        const auto x0 = static_cast<std::uint32_t>(std::floor(grid.X));
        const auto y0 = static_cast<std::uint32_t>(std::floor(grid.Y));
        const auto z0 = static_cast<std::uint32_t>(std::floor(grid.Z));
        const auto x1 = std::min(x0 + 1U, volume.CountX - 1U);
        const auto y1 = std::min(y0 + 1U, volume.CountY - 1U);
        const auto z1 = std::min(z0 + 1U, volume.CountZ - 1U);
        const auto fraction =
            Vector3{grid.X - static_cast<float>(x0), grid.Y - static_cast<float>(y0), grid.Z - static_cast<float>(z0)};
        std::array<Vector3, 9> coefficients{};
        float accumulatedWeight = 0.0F;
        for (std::uint32_t corner = 0; corner < 8U; ++corner)
        {
            const auto x = (corner & 1U) != 0U ? x1 : x0;
            const auto y = (corner & 2U) != 0U ? y1 : y0;
            const auto z = (corner & 4U) != 0U ? z1 : z0;
            const auto weightX = (corner & 1U) != 0U ? fraction.X : 1.0F - fraction.X;
            const auto weightY = (corner & 2U) != 0U ? fraction.Y : 1.0F - fraction.Y;
            const auto weightZ = (corner & 4U) != 0U ? fraction.Z : 1.0F - fraction.Z;
            const auto& probe = volume.Probes[(static_cast<std::size_t>(z) * volume.CountY + y) * volume.CountX + x];
            const auto weight = weightX * weightY * weightZ * probe.Validity;
            accumulatedWeight += weight;
            for (std::size_t coefficient = 0; coefficient < coefficients.size(); ++coefficient)
            {
                coefficients[coefficient].X += probe.Irradiance[coefficient].X * weight;
                coefficients[coefficient].Y += probe.Irradiance[coefficient].Y * weight;
                coefficients[coefficient].Z += probe.Irradiance[coefficient].Z * weight;
            }
        }
        if (accumulatedWeight <= 1.0e-6F)
            return std::nullopt;
        for (auto& coefficient : coefficients)
        {
            coefficient.X /= accumulatedWeight;
            coefficient.Y /= accumulatedWeight;
            coefficient.Z /= accumulatedWeight;
        }
        return coefficients;
    }

    std::optional<Vector3> SampleLightProbeIrradiance(const LightProbeVolumeDefinition& volume,
                                                      const Vector3 localPosition, const Vector3 localNormal)
    {
        if (!Math::IsFinite(localNormal))
            return std::nullopt;
        const auto coefficients = SampleLightProbeCoefficients(volume, localPosition);
        if (!coefficients)
            return std::nullopt;
        const auto normal = Normalize(localNormal);
        if (Length(normal) <= 1.0e-8F)
            return std::nullopt;
        constexpr float c0 = 0.28209479177387814F;
        constexpr float c1 = 0.4886025119029199F;
        constexpr float c2 = 1.0925484305920792F;
        constexpr float c3 = 0.31539156525252005F;
        constexpr float c4 = 0.5462742152960396F;
        const std::array basis{c0,
                               c1 * normal.Y,
                               c1 * normal.Z,
                               c1 * normal.X,
                               c2 * normal.X * normal.Y,
                               c2 * normal.Y * normal.Z,
                               c3 * (3.0F * normal.Z * normal.Z - 1.0F),
                               c2 * normal.X * normal.Z,
                               c4 * (normal.X * normal.X - normal.Y * normal.Y)};
        Vector3 result;
        for (std::size_t coefficient = 0; coefficient < coefficients->size(); ++coefficient)
        {
            result.X += (*coefficients)[coefficient].X * basis[coefficient];
            result.Y += (*coefficients)[coefficient].Y * basis[coefficient];
            result.Z += (*coefficients)[coefficient].Z * basis[coefficient];
        }
        return Vector3{std::max(0.0F, result.X), std::max(0.0F, result.Y), std::max(0.0F, result.Z)};
    }

    ShadowAtlasAllocator::ShadowAtlasAllocator(const std::uint16_t atlasSize, const std::uint16_t minimumTileSize)
        : m_AtlasSize(atlasSize), m_MinimumTileSize(minimumTileSize)
    {
        if (atlasSize == 0U || minimumTileSize == 0U || atlasSize % minimumTileSize != 0U ||
            (atlasSize & (atlasSize - 1U)) != 0U || (minimumTileSize & (minimumTileSize - 1U)) != 0U)
            throw std::invalid_argument("Shadow atlas and tile sizes must be compatible powers of two.");
    }

    std::span<const ShadowAtlasAllocation>
    ShadowAtlasAllocator::Allocate(const std::span<const ShadowAtlasRequest> requests)
    {
        std::vector<ShadowAtlasRequest> ordered(requests.begin(), requests.end());
        std::unordered_set<ShadowAtlasKey, ShadowAtlasKeyHash> keys;
        for (auto& request : ordered)
        {
            if (!request.Key.Light || !keys.emplace(request.Key).second)
                throw std::invalid_argument("Shadow atlas request keys must be valid and unique.");
            request.Resolution = std::clamp(request.Resolution, m_MinimumTileSize, m_AtlasSize);
            std::uint16_t rounded = m_MinimumTileSize;
            while (rounded < request.Resolution)
                rounded = static_cast<std::uint16_t>(rounded * 2U);
            request.Resolution = rounded;
        }
        std::ranges::sort(ordered,
                          [](const ShadowAtlasRequest& left, const ShadowAtlasRequest& right)
                          {
                              if (left.Importance != right.Importance)
                                  return left.Importance > right.Importance;
                              if (left.Resolution != right.Resolution)
                                  return left.Resolution > right.Resolution;
                              return left.Key < right.Key;
                          });
        const auto gridSize = static_cast<std::uint16_t>(m_AtlasSize / m_MinimumTileSize);
        std::vector<bool> occupied(static_cast<std::size_t>(gridSize) * gridSize);
        m_Allocations.clear();
        for (const auto& request : ordered)
        {
            const auto tile = static_cast<std::uint16_t>(request.Resolution / m_MinimumTileSize);
            std::uint16_t selectedX = gridSize;
            std::uint16_t selectedY = gridSize;
            if (const auto previous = m_Previous.find(request.Key);
                previous != m_Previous.end() && previous->second.Size == request.Resolution)
            {
                const auto x = static_cast<std::uint16_t>(previous->second.X / m_MinimumTileSize);
                const auto y = static_cast<std::uint16_t>(previous->second.Y / m_MinimumTileSize);
                if (Fits(occupied, gridSize, x, y, tile))
                {
                    selectedX = x;
                    selectedY = y;
                }
            }
            for (std::uint16_t y = 0; selectedX == gridSize && y + tile <= gridSize; y += tile)
            {
                for (std::uint16_t x = 0; x + tile <= gridSize; x += tile)
                {
                    if (Fits(occupied, gridSize, x, y, tile))
                    {
                        selectedX = x;
                        selectedY = y;
                        break;
                    }
                }
            }
            if (selectedX == gridSize)
                continue;
            Occupy(occupied, gridSize, selectedX, selectedY, tile);
            ShadowAtlasAllocation allocation;
            allocation.Key = request.Key;
            allocation.X = static_cast<std::uint16_t>(selectedX * m_MinimumTileSize);
            allocation.Y = static_cast<std::uint16_t>(selectedY * m_MinimumTileSize);
            allocation.Size = request.Resolution;
            const auto atlasSize = static_cast<float>(m_AtlasSize);
            const auto scale = static_cast<float>(allocation.Size) / atlasSize;
            allocation.ScaleOffset = {scale, scale, static_cast<float>(allocation.X) / atlasSize,
                                      static_cast<float>(allocation.Y) / atlasSize};
            m_Allocations.push_back(allocation);
        }
        m_Previous.clear();
        for (const auto& allocation : m_Allocations)
            m_Previous.emplace(allocation.Key, allocation);
        return m_Allocations;
    }
} // namespace Keire::Detail
