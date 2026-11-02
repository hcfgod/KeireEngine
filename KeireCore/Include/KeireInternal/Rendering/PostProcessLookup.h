#pragma once

#include "Keire/Math/Math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Keire::Detail
{
    // Mode-4 lookup contract: cube edge, effective blend, inverse strip width and inverse strip height.
    [[nodiscard]] inline Vector4 PostProcessLookupParameters(const std::uint32_t edge, const double weight,
                                                             const double gradingWeight, const bool available)
    {
        if (!available || (edge != 16 && edge != 32 && edge != 64) || !std::isfinite(weight) ||
            !std::isfinite(gradingWeight))
        {
            return {};
        }
        const auto size = static_cast<float>(edge);
        return {size, static_cast<float>(std::clamp(weight, 0.0, 1.0) * std::clamp(gradingWeight, 0.0, 1.0)),
                1.0F / (size * size), 1.0F / size};
    }
} // namespace Keire::Detail
