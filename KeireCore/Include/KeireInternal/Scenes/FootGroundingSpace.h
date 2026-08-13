#pragma once

#include "Keire/Math/Math.h"

#include <cmath>
#include <optional>

namespace Keire::Detail
{
    struct ModelFootGroundContact final
    {
        Vector3 Position;
        Vector3 Normal;
    };

    [[nodiscard]] inline std::optional<ModelFootGroundContact>
    ToModelFootGroundContact(const Matrix4& worldToModel, const Vector3 worldPosition, const Vector3 worldNormal,
                             const float worldFootOffset) noexcept
    {
        const auto normalLength =
            std::sqrt(worldNormal.X * worldNormal.X + worldNormal.Y * worldNormal.Y + worldNormal.Z * worldNormal.Z);
        if (!std::isfinite(normalLength) || normalLength <= 0.000001F || !std::isfinite(worldFootOffset) ||
            worldFootOffset < 0.0F)
            return std::nullopt;
        const Vector3 normalizedNormal{worldNormal.X / normalLength, worldNormal.Y / normalLength,
                                       worldNormal.Z / normalLength};
        const Vector3 solePosition{worldPosition.X + normalizedNormal.X * worldFootOffset,
                                   worldPosition.Y + normalizedNormal.Y * worldFootOffset,
                                   worldPosition.Z + normalizedNormal.Z * worldFootOffset};
        return ModelFootGroundContact{Math::TransformPoint(worldToModel, solePosition),
                                      Math::TransformDirection(worldToModel, normalizedNormal)};
    }

    [[nodiscard]] inline float WorldVerticalDistanceToModel(const Matrix4& worldToModel,
                                                            const float worldDistance) noexcept
    {
        const auto modelDistance = Math::TransformDirection(worldToModel, {0.0F, worldDistance, 0.0F});
        return std::sqrt(modelDistance.X * modelDistance.X + modelDistance.Y * modelDistance.Y +
                         modelDistance.Z * modelDistance.Z);
    }
} // namespace Keire::Detail
