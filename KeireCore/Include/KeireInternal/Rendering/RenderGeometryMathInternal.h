#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Math/Math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Keire::RenderBackend::GeometryDetail
{
    struct ClipPoint final
    {
        float X;
        float Y;
        float Z;
        float W;
    };

    struct FrustumPlanes final
    {
        std::array<ClipPoint, 6> Values;
    };

    struct ProjectedRectangle final
    {
        float MinimumX = 0.0F;
        float MinimumY = 0.0F;
        float MaximumX = 0.0F;
        float MaximumY = 0.0F;

        [[nodiscard]] float Area() const noexcept
        {
            return std::max(0.0F, MaximumX - MinimumX) * std::max(0.0F, MaximumY - MinimumY);
        }
    };

    [[nodiscard]] inline ClipPoint TransformClip(const Matrix4& matrix, const Vector3 point) noexcept
    {
        const auto& value = matrix.Elements;
        return {value[0] * point.X + value[4] * point.Y + value[8] * point.Z + value[12],
                value[1] * point.X + value[5] * point.Y + value[9] * point.Z + value[13],
                value[2] * point.X + value[6] * point.Y + value[10] * point.Z + value[14],
                value[3] * point.X + value[7] * point.Y + value[11] * point.Z + value[15]};
    }

    [[nodiscard]] inline ProjectedRectangle ProjectedBoundsPixels(const Matrix4& clipFromLocal, const MeshBounds bounds,
                                                                  const std::uint32_t width,
                                                                  const std::uint32_t height) noexcept
    {
        float minimumX = static_cast<float>(width);
        float minimumY = static_cast<float>(height);
        float maximumX = 0.0F;
        float maximumY = 0.0F;
        for (std::uint32_t corner = 0; corner < 8U; ++corner)
        {
            const Vector3 point{(corner & 1U) != 0U ? bounds.Maximum.X : bounds.Minimum.X,
                                (corner & 2U) != 0U ? bounds.Maximum.Y : bounds.Minimum.Y,
                                (corner & 4U) != 0U ? bounds.Maximum.Z : bounds.Minimum.Z};
            const auto clip = TransformClip(clipFromLocal, point);
            if (!std::isfinite(clip.X) || !std::isfinite(clip.Y) || !std::isfinite(clip.W))
                return {};
            if (clip.W <= 0.00001F)
                return {0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height)};
            const float x = (clip.X / clip.W * 0.5F + 0.5F) * static_cast<float>(width);
            const float y = (-clip.Y / clip.W * 0.5F + 0.5F) * static_cast<float>(height);
            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
        minimumX = std::clamp(minimumX, 0.0F, static_cast<float>(width));
        minimumY = std::clamp(minimumY, 0.0F, static_cast<float>(height));
        maximumX = std::clamp(maximumX, 0.0F, static_cast<float>(width));
        maximumY = std::clamp(maximumY, 0.0F, static_cast<float>(height));
        return {minimumX, minimumY, maximumX, maximumY};
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

    [[nodiscard]] inline FrustumPlanes BuildFrustumPlanes(const Matrix4& clipFromLocal) noexcept
    {
        const auto& matrix = clipFromLocal.Elements;
        return {std::array<ClipPoint, 6>{
            ClipPoint{matrix[3] + matrix[0], matrix[7] + matrix[4], matrix[11] + matrix[8], matrix[15] + matrix[12]},
            ClipPoint{matrix[3] - matrix[0], matrix[7] - matrix[4], matrix[11] - matrix[8], matrix[15] - matrix[12]},
            ClipPoint{matrix[3] + matrix[1], matrix[7] + matrix[5], matrix[11] + matrix[9], matrix[15] + matrix[13]},
            ClipPoint{matrix[3] - matrix[1], matrix[7] - matrix[5], matrix[11] - matrix[9], matrix[15] - matrix[13]},
            ClipPoint{matrix[2], matrix[6], matrix[10], matrix[14]},
            ClipPoint{matrix[3] - matrix[2], matrix[7] - matrix[6], matrix[11] - matrix[10], matrix[15] - matrix[14]}}};
    }

    [[nodiscard]] inline bool Encloses(const MeshBounds outer, const MeshBounds inner) noexcept
    {
        return outer.Minimum.X <= outer.Maximum.X && outer.Minimum.Y <= outer.Maximum.Y &&
               outer.Minimum.Z <= outer.Maximum.Z && inner.Minimum.X <= inner.Maximum.X &&
               inner.Minimum.Y <= inner.Maximum.Y && inner.Minimum.Z <= inner.Maximum.Z &&
               outer.Minimum.X <= inner.Minimum.X && outer.Minimum.Y <= inner.Minimum.Y &&
               outer.Minimum.Z <= inner.Minimum.Z && outer.Maximum.X >= inner.Maximum.X &&
               outer.Maximum.Y >= inner.Maximum.Y && outer.Maximum.Z >= inner.Maximum.Z;
    }

    [[nodiscard]] inline bool IntersectsFrustum(const FrustumPlanes& frustum, const MeshBounds bounds) noexcept
    {
        for (const auto plane : frustum.Values)
        {
            const auto x = plane.X >= 0.0F ? bounds.Maximum.X : bounds.Minimum.X;
            const auto y = plane.Y >= 0.0F ? bounds.Maximum.Y : bounds.Minimum.Y;
            const auto z = plane.Z >= 0.0F ? bounds.Maximum.Z : bounds.Minimum.Z;
            if (plane.X * x + plane.Y * y + plane.Z * z + plane.W < 0.0F)
                return false;
        }
        return true;
    }

    [[nodiscard]] inline bool IntersectsFrustum(const Matrix4& clipFromLocal, const MeshBounds bounds) noexcept
    {
        return IntersectsFrustum(BuildFrustumPlanes(clipFromLocal), bounds);
    }

    [[nodiscard]] inline bool IsFrustumVisible(const Matrix4& clipFromLocal, const MeshBounds bounds,
                                               const bool alwaysVisible) noexcept
    {
        return alwaysVisible || IntersectsFrustum(clipFromLocal, bounds);
    }

    [[nodiscard]] inline bool IsFrustumVisible(const FrustumPlanes& frustum, const MeshBounds bounds,
                                               const bool alwaysVisible) noexcept
    {
        return alwaysVisible || IntersectsFrustum(frustum, bounds);
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
