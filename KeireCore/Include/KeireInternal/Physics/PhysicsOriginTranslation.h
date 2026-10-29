#pragma once

#include "Keire/Math/Math.h"
#include "Keire/Math/Vector3d.h"
#include "KeireInternal/Math/RelativePositionTranslation.h"

namespace Keire::Detail
{
    [[nodiscard]] inline Vector3 TranslatePhysicsPosition(const Vector3 position, const Vector3d originDelta,
                                                          const double maximumError)
    {
        // Subtract the two origins first; reconstructing an absolute double can erase small local offsets.
        return TranslateRelativePosition(position, originDelta, maximumError);
    }

    [[nodiscard]] inline Matrix4 TranslatePhysicsMatrix(Matrix4 matrix, const Vector3d originDelta,
                                                        const double maximumError)
    {
        const auto position = TranslatePhysicsPosition({matrix.Elements[12], matrix.Elements[13], matrix.Elements[14]},
                                                       originDelta, maximumError);
        matrix.Elements[12] = position.X;
        matrix.Elements[13] = position.Y;
        matrix.Elements[14] = position.Z;
        return matrix;
    }
} // namespace Keire::Detail
