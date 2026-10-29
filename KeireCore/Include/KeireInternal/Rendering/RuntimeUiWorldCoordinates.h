#pragma once

#include "Keire/Math/Vector3d.h"

#include <limits>
#include <stdexcept>

namespace Keire::Internal
{
    [[nodiscard]] inline Matrix4 RuntimeUiWorldInCameraFrame(const Matrix4& world, const Vector3d sourceOrigin,
                                                             const Vector3d cameraOrigin)
    {
        if (!Math::IsFinite(world) || !Math::IsFinite(sourceOrigin) || !Math::IsFinite(cameraOrigin))
        {
            throw std::invalid_argument("World UI requires a finite transform and coordinate origins.");
        }
        const Vector3d translation{(sourceOrigin.X - cameraOrigin.X) + world.Elements[12],
                                   (sourceOrigin.Y - cameraOrigin.Y) + world.Elements[13],
                                   (sourceOrigin.Z - cameraOrigin.Z) + world.Elements[14]};
        const auto local = Math::RelativeFloat(translation, {}, std::numeric_limits<double>::max());
        auto result = world;
        result.Elements[12] = local.X;
        result.Elements[13] = local.Y;
        result.Elements[14] = local.Z;
        return result;
    }
} // namespace Keire::Internal
