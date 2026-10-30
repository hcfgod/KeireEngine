#pragma once

#include "Keire/Math/Math.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Keire::RenderBackend
{
    inline constexpr float DirectionalShadowDepthBiasConstant = 2.0F;
    inline constexpr float DirectionalShadowDepthBiasSlope = 2.0F;

    struct AnchoredDepthTransform
    {
        Matrix4 Transform;
        Vector4 LocalAnchor;
    };

    [[nodiscard]] AnchoredDepthTransform BuildAnchoredDepthTransform(const Matrix4& viewProjection,
                                                                     const Matrix4& model);

    [[nodiscard]] std::array<Vector3, 4> BuildShadowSliceCorners(const Matrix4& projection, const Matrix4& view,
                                                                 float viewDepth);

    [[nodiscard]] std::vector<float> BuildPracticalCascadeSplits(float nearPlane, float shadowDistance,
                                                                 std::uint32_t cascadeCount, float splitLambda);
    [[nodiscard]] Vector2 StabilizeShadowCenter(Vector2 lightSpaceCenter, float cascadeDiameter,
                                                std::uint32_t resolution);
    [[nodiscard]] Vector3 StabilizeShadowCenter(Vector3 worldCenter, Vector3 lightRight, Vector3 lightUp,
                                                float cascadeDiameter, std::uint32_t resolution);
} // namespace Keire::RenderBackend
