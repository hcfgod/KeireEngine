#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Math/Math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace Keire::RenderBackend::GeometryDetail
{
    struct ClipPoint final
    {
        float X;
        float Y;
        float Z;
        float W;
    };

    [[nodiscard]] inline ClipPoint TransformClip(const Matrix4& matrix, const Vector3 point) noexcept
    {
        const auto& value = matrix.Elements;
        return {value[0] * point.X + value[4] * point.Y + value[8] * point.Z + value[12],
                value[1] * point.X + value[5] * point.Y + value[9] * point.Z + value[13],
                value[2] * point.X + value[6] * point.Y + value[10] * point.Z + value[14],
                value[3] * point.X + value[7] * point.Y + value[11] * point.Z + value[15]};
    }

    [[nodiscard]] inline Vector3 Add(const Vector3 left, const Vector3 right) noexcept
    {
        return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
    }

    [[nodiscard]] inline Vector3 Subtract(const Vector3 left, const Vector3 right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
    }

    [[nodiscard]] inline Vector3 Scale(const Vector3 value, const float scale) noexcept
    {
        return {value.X * scale, value.Y * scale, value.Z * scale};
    }

    [[nodiscard]] inline float Length(const Vector3 value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    }

    [[nodiscard]] inline Vector3 Cross(const Vector3 left, const Vector3 right) noexcept
    {
        return {left.Y * right.Z - left.Z * right.Y, left.Z * right.X - left.X * right.Z,
                left.X * right.Y - left.Y * right.X};
    }

    [[nodiscard]] inline Vector3 NormalizeOr(const Vector3 value, const Vector3 fallback) noexcept
    {
        const auto length = Length(value);
        return length > 0.000001F ? Scale(value, 1.0F / length) : fallback;
    }

    [[nodiscard]] inline bool IntersectsFrustum(const Matrix4& clipFromLocal, const MeshBounds bounds) noexcept
    {
        const std::array corners{Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                 Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Minimum.Z},
                                 Vector3{bounds.Minimum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Vector3{bounds.Maximum.X, bounds.Minimum.Y, bounds.Maximum.Z},
                                 Vector3{bounds.Minimum.X, bounds.Maximum.Y, bounds.Maximum.Z},
                                 Vector3{bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
        std::array<ClipPoint, corners.size()> clip{};
        std::ranges::transform(corners, clip.begin(),
                               [&](const auto corner) { return TransformClip(clipFromLocal, corner); });
        const auto all = [&](const auto predicate) { return std::ranges::all_of(clip, predicate); };
        return !all([](const auto point) { return point.X < -point.W; }) &&
               !all([](const auto point) { return point.X > point.W; }) &&
               !all([](const auto point) { return point.Y < -point.W; }) &&
               !all([](const auto point) { return point.Y > point.W; }) &&
               !all([](const auto point) { return point.Z < 0.0F; }) &&
               !all([](const auto point) { return point.Z > point.W; });
    }

    [[nodiscard]] inline float ProjectedHeight(const Matrix4& viewFromLocal, const Matrix4& projection,
                                               const MeshBounds bounds) noexcept
    {
        const Vector3 center{(bounds.Minimum.X + bounds.Maximum.X) * 0.5F, (bounds.Minimum.Y + bounds.Maximum.Y) * 0.5F,
                             (bounds.Minimum.Z + bounds.Maximum.Z) * 0.5F};
        const Vector3 extent{(bounds.Maximum.X - bounds.Minimum.X) * 0.5F, (bounds.Maximum.Y - bounds.Minimum.Y) * 0.5F,
                             (bounds.Maximum.Z - bounds.Minimum.Z) * 0.5F};
        const auto viewCenter = Math::TransformPoint(viewFromLocal, center);
        const float localRadius = std::sqrt(extent.X * extent.X + extent.Y * extent.Y + extent.Z * extent.Z);
        const auto& matrix = viewFromLocal.Elements;
        const float scaleX = std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2]);
        const float scaleY = std::sqrt(matrix[4] * matrix[4] + matrix[5] * matrix[5] + matrix[6] * matrix[6]);
        const float scaleZ = std::sqrt(matrix[8] * matrix[8] + matrix[9] * matrix[9] + matrix[10] * matrix[10]);
        const float radius = localRadius * std::max({scaleX, scaleY, scaleZ});
        return viewCenter.Z > 0.0001F ? 2.0F * radius * std::abs(projection.Elements[5]) / viewCenter.Z : 1.0F;
    }
} // namespace Keire::RenderBackend::GeometryDetail
