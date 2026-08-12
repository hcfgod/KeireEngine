#pragma once

#include "Keire/Math/Math.h"

namespace KeireEditor
{
    [[nodiscard]] Keire::Vector3 ContinuousEulerAngles(Keire::Quaternion rotation, Keire::Vector3 referenceDegrees);
    [[nodiscard]] bool SameRotation(Keire::Quaternion left, Keire::Quaternion right, float tolerance = 0.00001F);
} // namespace KeireEditor
