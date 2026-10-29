#pragma once

#include "Keire/Physics/PhysicsSystem.h"

#include <cmath>

namespace Keire::Detail
{
    [[nodiscard]] inline bool ValidMotionSettings(const PhysicsMotionSettings& settings) noexcept
    {
        return std::isfinite(settings.LinearDamping) && settings.LinearDamping >= 0.0F &&
               std::isfinite(settings.AngularDamping) && settings.AngularDamping >= 0.0F &&
               std::isfinite(settings.MaximumLinearSpeed) && settings.MaximumLinearSpeed > 0.0F &&
               std::isfinite(settings.MaximumAngularSpeed) && settings.MaximumAngularSpeed > 0.0F;
    }
} // namespace Keire::Detail
