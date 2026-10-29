#pragma once

#include "Keire/Math/Vector3d.h"

#include <array>
#include <stdexcept>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline std::array<double, 3>
    EnvironmentRelativeObserver(const Vector3 observer, const Vector3d renderOrigin, const Vector3d environmentAnchor)
    {
        if (!Math::IsFinite(observer) || !Math::IsFinite(renderOrigin) || !Math::IsFinite(environmentAnchor))
        {
            throw std::invalid_argument("Environment capture requires finite observer coordinates and origins.");
        }
        // Preserve the local offset by cancelling large origins before adding it.
        return {(renderOrigin.X - environmentAnchor.X) + observer.X,
                (renderOrigin.Y - environmentAnchor.Y) + observer.Y,
                (renderOrigin.Z - environmentAnchor.Z) + observer.Z};
    }
} // namespace Keire::RenderBackend
