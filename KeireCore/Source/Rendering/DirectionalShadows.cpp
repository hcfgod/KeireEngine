#include "KeireInternal/Rendering/DirectionalShadowInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"

#include <cmath>
#include <stdexcept>

namespace Keire::RenderBackend
{
    AnchoredDepthTransform BuildAnchoredDepthTransform(const Matrix4& viewProjection, const Matrix4& model)
    {
        AnchoredDepthTransform result{Math::Multiply(viewProjection, model), {}};
        constexpr float anchorMagnitudeThreshold = 65536.0F;
        bool requiresAnchor = false;
        for (const float element : model.Elements)
        {
            requiresAnchor |= std::abs(element) > anchorMagnitudeThreshold;
        }
        if (!requiresAnchor)
        {
            return result;
        }

        // Move the vertex arithmetic near the render origin before projection. A planet-sized
        // model otherwise subtracts huge clip coordinates to produce a tiny cascade depth.
        Matrix4 inverseModel;
        try
        {
            inverseModel = Math::Inverse(model);
        }
        catch (const std::invalid_argument&)
        {
            // A zero-scale object must not abort the entire shadow pass.
            return result;
        }
        const auto anchor = GeometryDetail::TransformClip(inverseModel, {});
        if (!std::isfinite(anchor.X) || !std::isfinite(anchor.Y) || !std::isfinite(anchor.Z) ||
            !std::isfinite(anchor.W) || std::abs(anchor.W) < 1.0e-8F)
        {
            return result;
        }
        result.LocalAnchor = {anchor.X / anchor.W, anchor.Y / anchor.W, anchor.Z / anchor.W, 0.0F};
        const std::array<double, 4> localAnchor{result.LocalAnchor.X, result.LocalAnchor.Y, result.LocalAnchor.Z, 1.0};
        std::array<double, 16> combined{};
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t row = 0; row < 4; ++row)
            {
                for (std::size_t inner = 0; inner < 4; ++inner)
                {
                    combined[column * 4 + row] += static_cast<double>(viewProjection.Elements[inner * 4 + row]) *
                                                  model.Elements[column * 4 + inner];
                }
                result.Transform.Elements[column * 4 + row] = static_cast<float>(combined[column * 4 + row]);
            }
        }
        for (std::size_t row = 0; row < 4; ++row)
        {
            double translation = 0.0;
            for (std::size_t column = 0; column < 4; ++column)
            {
                translation += combined[column * 4 + row] * localAnchor[column];
            }
            result.Transform.Elements[12 + row] = static_cast<float>(translation);
        }
        return result;
    }

    std::array<Vector3, 4> BuildShadowSliceCorners(const Matrix4& projection, const Matrix4& view,
                                                   const float viewDepth)
    {
        const auto inverseProjection = Math::Inverse(projection);
        const auto inverseView = Math::Inverse(view);
        constexpr std::array<Vector2, 4> coordinates{{{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}};
        std::array<Vector3, 4> corners{};
        for (std::size_t index = 0; index < corners.size(); ++index)
        {
            // The far clip point can be at infinity after float projection rounding.
            // Two interior points define the same ray without dividing by its zero W.
            const auto nearClip =
                GeometryDetail::TransformClip(inverseProjection, {coordinates[index].X, coordinates[index].Y, 0.0F});
            const auto middleClip =
                GeometryDetail::TransformClip(inverseProjection, {coordinates[index].X, coordinates[index].Y, 0.5F});
            const Vector3 nearPoint{nearClip.X / nearClip.W, nearClip.Y / nearClip.W, nearClip.Z / nearClip.W};
            const Vector3 middlePoint{middleClip.X / middleClip.W, middleClip.Y / middleClip.W,
                                      middleClip.Z / middleClip.W};
            const float ratio = (viewDepth - nearPoint.Z) / (middlePoint.Z - nearPoint.Z);
            const Vector3 viewPoint{std::lerp(nearPoint.X, middlePoint.X, ratio),
                                    std::lerp(nearPoint.Y, middlePoint.Y, ratio), viewDepth};
            const auto world = GeometryDetail::TransformClip(inverseView, viewPoint);
            corners[index] = {world.X / world.W, world.Y / world.W, world.Z / world.W};
        }
        return corners;
    }

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

    Vector3 StabilizeShadowCenter(const Vector3 worldCenter, const Vector3 lightRight, const Vector3 lightUp,
                                  const float cascadeDiameter, const std::uint32_t resolution)
    {
        const auto dot = [](const Vector3 left, const Vector3 right) noexcept
        { return left.X * right.X + left.Y * right.Y + left.Z * right.Z; };
        const Vector2 lightSpaceCenter{dot(worldCenter, lightRight), dot(worldCenter, lightUp)};
        const auto stabilized = StabilizeShadowCenter(lightSpaceCenter, cascadeDiameter, resolution);
        const auto addScaled = [](const Vector3 value, const Vector3 direction, const float scale) noexcept
        {
            return Vector3{value.X + direction.X * scale, value.Y + direction.Y * scale, value.Z + direction.Z * scale};
        };
        return addScaled(addScaled(worldCenter, lightRight, stabilized.X - lightSpaceCenter.X), lightUp,
                         stabilized.Y - lightSpaceCenter.Y);
    }
} // namespace Keire::RenderBackend
