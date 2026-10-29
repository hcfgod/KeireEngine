#pragma once

#include "Keire/Math/Vector3d.h"

#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    [[nodiscard]] inline Vector3 TranslateRelativePosition(const Vector3 position, const Vector3d delta,
                                                           const double maximumError)
    {
        const Vector3d sum{delta.X + position.X, delta.Y + position.Y, delta.Z + position.Z};
        const auto result = Math::RelativeFloat(sum, {}, maximumError);
        const auto validate =
            [&](const double translation, const double local, const double combined, const float narrowed)
        {
            // TwoSum retains the addition residual that a narrowing-only check cannot observe.
            // Preserve the operation order: reassociation would erase the residual again.
            const double virtualLocal = combined - translation;
            const double residual = (translation - (combined - virtualLocal)) + (local - virtualLocal);
            const double error = (static_cast<double>(narrowed) - combined) - residual;
            if (!std::isfinite(error) || std::abs(error) > maximumError)
            {
                throw std::out_of_range("Origin translation exceeds the positional precision budget.");
            }
        };
        validate(delta.X, position.X, sum.X, result.X);
        validate(delta.Y, position.Y, sum.Y, result.Y);
        validate(delta.Z, position.Z, sum.Z, result.Z);
        return result;
    }
} // namespace Keire::Detail
