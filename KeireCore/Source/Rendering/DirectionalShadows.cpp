#include "KeireInternal/Rendering/DirectionalShadowInternal.h"

#include <cmath>
#include <stdexcept>

namespace Keire::RenderBackend
{
    std::vector<float> BuildPracticalCascadeSplits(const float nearPlane, const float shadowDistance,
                                                   const std::uint32_t cascadeCount, const float splitLambda)
    {
        if (!std::isfinite(nearPlane) || !std::isfinite(shadowDistance) || !std::isfinite(splitLambda) ||
            nearPlane <= 0.0F || shadowDistance <= nearPlane || cascadeCount < 1U || cascadeCount > 4U ||
            splitLambda < 0.0F || splitLambda > 1.0F)
            throw std::invalid_argument("Directional cascade split settings are invalid.");

        std::vector<float> result;
        result.reserve(cascadeCount);
        for (std::uint32_t cascade = 1; cascade <= cascadeCount; ++cascade)
        {
            const float ratio = static_cast<float>(cascade) / static_cast<float>(cascadeCount);
            const float logarithmic = nearPlane * std::pow(shadowDistance / nearPlane, ratio);
            const float uniform = nearPlane + (shadowDistance - nearPlane) * ratio;
            result.push_back(std::lerp(uniform, logarithmic, splitLambda));
        }
        result.back() = shadowDistance;
        return result;
    }

    Vector2 StabilizeShadowCenter(const Vector2 lightSpaceCenter, const float cascadeDiameter,
                                  const std::uint32_t resolution)
    {
        if (!std::isfinite(lightSpaceCenter.X) || !std::isfinite(lightSpaceCenter.Y) ||
            !std::isfinite(cascadeDiameter) || cascadeDiameter <= 0.0F || resolution == 0U)
            throw std::invalid_argument("Directional cascade stabilization settings are invalid.");
        const float texelSize = cascadeDiameter / static_cast<float>(resolution);
        return {std::round(lightSpaceCenter.X / texelSize) * texelSize,
                std::round(lightSpaceCenter.Y / texelSize) * texelSize};
    }
} // namespace Keire::RenderBackend
