#pragma once

#include "Keire/Math/Vector3d.h"

#include <cmath>
#include <limits>
#include <optional>

namespace Keire::RenderBackend
{
    struct GridOriginUniforms final
    {
        Vector4 PhaseAndPlane;
        Vector4 Axes;
    };

    [[nodiscard]] inline std::optional<GridOriginUniforms> MakeGridOriginUniforms(const Vector3d origin,
                                                                                  const Vector3 localObserver,
                                                                                  const float fadeDistance,
                                                                                  const float majorSpacing)
    {
        if (!Math::IsFinite(origin) || !Math::IsFinite(localObserver) || !std::isfinite(fadeDistance) ||
            fadeDistance <= 0.0F || !std::isfinite(majorSpacing) || majorSpacing <= 0.0F)
        {
            return std::nullopt;
        }
        const double observerHeight = origin.Y + static_cast<double>(localObserver.Y);
        if (!std::isfinite(observerHeight) || std::abs(observerHeight) > fadeDistance ||
            std::abs(origin.Y) > std::numeric_limits<float>::max())
        {
            return std::nullopt;
        }
        GridOriginUniforms result;
        // Major spacing is an integer multiple of minor spacing. One bounded phase
        // therefore preserves both grids without reconstructing a large float position.
        result.PhaseAndPlane = {static_cast<float>(std::fmod(origin.X, majorSpacing)), static_cast<float>(-origin.Y),
                                static_cast<float>(std::fmod(origin.Z, majorSpacing)), 0.0F};
        const bool xAxisRepresentable = std::abs(origin.X) <= std::numeric_limits<float>::max();
        const bool zAxisRepresentable = std::abs(origin.Z) <= std::numeric_limits<float>::max();
        result.Axes = {xAxisRepresentable ? static_cast<float>(-origin.X) : 0.0F,
                       zAxisRepresentable ? static_cast<float>(-origin.Z) : 0.0F, xAxisRepresentable ? 1.0F : 0.0F,
                       zAxisRepresentable ? 1.0F : 0.0F};
        return result;
    }
} // namespace Keire::RenderBackend
