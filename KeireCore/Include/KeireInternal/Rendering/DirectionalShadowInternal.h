#pragma once

#include "Keire/Math/Math.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Keire::RenderBackend
{
    inline constexpr float DirectionalShadowDepthBiasConstant = 2.0F;
    inline constexpr float DirectionalShadowDepthBiasSlope = 2.0F;

    [[nodiscard]] std::vector<float> BuildPracticalCascadeSplits(float nearPlane, float shadowDistance,
                                                                 std::uint32_t cascadeCount, float splitLambda);
    [[nodiscard]] Vector2 StabilizeShadowCenter(Vector2 lightSpaceCenter, float cascadeDiameter,
                                                std::uint32_t resolution);
} // namespace Keire::RenderBackend
