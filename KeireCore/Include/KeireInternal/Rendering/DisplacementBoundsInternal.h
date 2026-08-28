#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Math/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace Keire::RenderBackend::DisplacementBounds
{
    [[nodiscard]] inline bool IsKnown(const std::optional<float> radius) noexcept
    {
        return radius && std::isfinite(*radius) && *radius >= 0.0F;
    }

    [[nodiscard]] inline std::optional<MeshBounds>
    WorldBounds(const MeshBounds& localBounds, const Matrix4& localToWorld, const std::optional<float> radius) noexcept
    {
        if (!IsKnown(radius) || !Math::IsFinite(localBounds.Minimum) || !Math::IsFinite(localBounds.Maximum) ||
            !Math::IsFinite(localToWorld) || localBounds.Minimum.X > localBounds.Maximum.X ||
            localBounds.Minimum.Y > localBounds.Maximum.Y || localBounds.Minimum.Z > localBounds.Maximum.Z)
        {
            return std::nullopt;
        }

        const auto maximum = std::numeric_limits<float>::max();
        MeshBounds result{{maximum, maximum, maximum}, {-maximum, -maximum, -maximum}};
        for (std::uint32_t corner = 0; corner < 8U; ++corner)
        {
            const Vector3 local{(corner & 1U) != 0U ? localBounds.Maximum.X : localBounds.Minimum.X,
                                (corner & 2U) != 0U ? localBounds.Maximum.Y : localBounds.Minimum.Y,
                                (corner & 4U) != 0U ? localBounds.Maximum.Z : localBounds.Minimum.Z};
            const auto world = Math::TransformPoint(localToWorld, local);
            if (!Math::IsFinite(world))
                return std::nullopt;
            result.Minimum = {std::min(result.Minimum.X, world.X), std::min(result.Minimum.Y, world.Y),
                              std::min(result.Minimum.Z, world.Z)};
            result.Maximum = {std::max(result.Maximum.X, world.X), std::max(result.Maximum.Y, world.Y),
                              std::max(result.Maximum.Z, world.Z)};
        }
        const Vector3 padding{*radius, *radius, *radius};
        result.Minimum = {result.Minimum.X - padding.X, result.Minimum.Y - padding.Y, result.Minimum.Z - padding.Z};
        result.Maximum = {result.Maximum.X + padding.X, result.Maximum.Y + padding.Y, result.Maximum.Z + padding.Z};
        return Math::IsFinite(result.Minimum) && Math::IsFinite(result.Maximum) ? std::optional{result} : std::nullopt;
    }

    [[nodiscard]] inline bool WhollyContained(const MeshBounds& localBounds, const Matrix4& localToWorld,
                                              const Matrix4& worldToVolume, const Vector3 volumeExtents,
                                              const std::optional<float> radius) noexcept
    {
        if (!IsKnown(radius) || !Math::IsFinite(localBounds.Minimum) || !Math::IsFinite(localBounds.Maximum) ||
            !Math::IsFinite(localToWorld) || !Math::IsFinite(worldToVolume) || !Math::IsFinite(volumeExtents) ||
            volumeExtents.X <= 0.0F || volumeExtents.Y <= 0.0F || volumeExtents.Z <= 0.0F ||
            localBounds.Minimum.X > localBounds.Maximum.X || localBounds.Minimum.Y > localBounds.Maximum.Y ||
            localBounds.Minimum.Z > localBounds.Maximum.Z)
        {
            return false;
        }

        const auto& matrix = worldToVolume.Elements;
        const Vector3 margin{*radius * std::sqrt(matrix[0] * matrix[0] + matrix[4] * matrix[4] + matrix[8] * matrix[8]),
                             *radius * std::sqrt(matrix[1] * matrix[1] + matrix[5] * matrix[5] + matrix[9] * matrix[9]),
                             *radius *
                                 std::sqrt(matrix[2] * matrix[2] + matrix[6] * matrix[6] + matrix[10] * matrix[10])};
        if (!Math::IsFinite(margin))
            return false;

        constexpr float tolerance = 1.0e-4F;
        for (std::uint32_t corner = 0; corner < 8U; ++corner)
        {
            const Vector3 local{(corner & 1U) != 0U ? localBounds.Maximum.X : localBounds.Minimum.X,
                                (corner & 2U) != 0U ? localBounds.Maximum.Y : localBounds.Minimum.Y,
                                (corner & 4U) != 0U ? localBounds.Maximum.Z : localBounds.Minimum.Z};
            const auto volume = Math::TransformPoint(worldToVolume, Math::TransformPoint(localToWorld, local));
            if (!Math::IsFinite(volume) || std::abs(volume.X) + margin.X > volumeExtents.X + tolerance ||
                std::abs(volume.Y) + margin.Y > volumeExtents.Y + tolerance ||
                std::abs(volume.Z) + margin.Z > volumeExtents.Z + tolerance)
            {
                return false;
            }
        }
        return true;
    }
} // namespace Keire::RenderBackend::DisplacementBounds
