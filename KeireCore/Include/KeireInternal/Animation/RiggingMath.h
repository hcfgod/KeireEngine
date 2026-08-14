
#pragma once

#include "Keire/Animation/RiggingSystem.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace Keire::RiggingDetail
{
    inline constexpr float Epsilon = 0.000001F;

    [[nodiscard]] inline Vector3 Add(const Vector3 left, const Vector3 right) noexcept
    {
        return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
    }

    [[nodiscard]] inline Vector3 Subtract(const Vector3 left, const Vector3 right) noexcept
    {
        return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
    }

    [[nodiscard]] inline Vector3 Multiply(const Vector3 value, const float scalar) noexcept
    {
        return {value.X * scalar, value.Y * scalar, value.Z * scalar};
    }

    [[nodiscard]] inline float Dot(const Vector3 left, const Vector3 right) noexcept
    {
        return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
    }

    [[nodiscard]] inline Vector3 Cross(const Vector3 left, const Vector3 right) noexcept
    {
        return {left.Y * right.Z - left.Z * right.Y, left.Z * right.X - left.X * right.Z,
                left.X * right.Y - left.Y * right.X};
    }

    [[nodiscard]] inline float Length(const Vector3 value) noexcept { return std::sqrt(Dot(value, value)); }

    [[nodiscard]] inline Vector3 Normalize(const Vector3 value, const Vector3 fallback = {0.0F, 1.0F, 0.0F}) noexcept
    {
        const auto length = Length(value);
        return length > Epsilon ? Multiply(value, 1.0F / length) : fallback;
    }

    [[nodiscard]] inline Quaternion Multiply(const Quaternion left, const Quaternion right) noexcept
    {
        return {left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
                left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
                left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
                left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z};
    }

    [[nodiscard]] inline Quaternion Conjugate(const Quaternion value) noexcept
    {
        return {-value.X, -value.Y, -value.Z, value.W};
    }

    [[nodiscard]] inline Quaternion Normalize(const Quaternion value) noexcept
    {
        const auto length = std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z + value.W * value.W);
        if (length <= Epsilon)
            return {};
        const auto inverse = 1.0F / length;
        return {value.X * inverse, value.Y * inverse, value.Z * inverse, value.W * inverse};
    }

    [[nodiscard]] inline Vector3 Rotate(const Quaternion rotation, const Vector3 value) noexcept
    {
        const auto normalized = Normalize(rotation);
        const auto rotated = Multiply(Multiply(normalized, {value.X, value.Y, value.Z, 0.0F}), Conjugate(normalized));
        return {rotated.X, rotated.Y, rotated.Z};
    }

    [[nodiscard]] inline Quaternion FromTo(const Vector3 source, const Vector3 destination) noexcept
    {
        const auto from = Normalize(source);
        const auto to = Normalize(destination);
        const auto cosine = std::clamp(Dot(from, to), -1.0F, 1.0F);
        if (cosine > 0.999999F)
            return {};
        if (cosine < -0.999999F)
        {
            auto axis = Cross(from, {1.0F, 0.0F, 0.0F});
            if (Length(axis) <= Epsilon)
                axis = Cross(from, {0.0F, 1.0F, 0.0F});
            axis = Normalize(axis);
            return {axis.X, axis.Y, axis.Z, 0.0F};
        }
        const auto axis = Cross(from, to);
        return Normalize({axis.X, axis.Y, axis.Z, 1.0F + cosine});
    }

    [[nodiscard]] inline Quaternion Nlerp(const Quaternion left, const Quaternion right, const float amount) noexcept
    {
        const auto t = std::clamp(amount, 0.0F, 1.0F);
        const auto dot = left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W;
        const auto sign = dot < 0.0F ? -1.0F : 1.0F;
        return Normalize({left.X + (right.X * sign - left.X) * t, left.Y + (right.Y * sign - left.Y) * t,
                          left.Z + (right.Z * sign - left.Z) * t, left.W + (right.W * sign - left.W) * t});
    }

    [[nodiscard]] inline std::vector<Matrix4> WorldMatrices(const SkeletonAsset& skeleton,
                                                            const std::span<const BoneTransform> localPose)
    {
        if (skeleton.Bones().size() != localPose.size())
            return {};
        std::vector<Matrix4> result(localPose.size());
        for (std::size_t index = 0; index < localPose.size(); ++index)
        {
            result[index] =
                Math::ComposeTransform(localPose[index].Translation, localPose[index].Rotation, localPose[index].Scale);
            const auto parent = skeleton.Bones()[index].Parent;
            if (parent >= 0)
                result[index] = Math::Multiply(result[static_cast<std::size_t>(parent)], result[index]);
        }
        return result;
    }

    [[nodiscard]] inline bool MatrixRotation(const Matrix4& matrix, Quaternion& rotation) noexcept
    {
        Vector3 position;
        Vector3 scale;
        return Math::DecomposeTransform(matrix, position, rotation, scale);
    }

    [[nodiscard]] inline bool SetBoneModelRotation(const SkeletonAsset& skeleton,
                                                   const std::span<BoneTransform> localPose, const std::uint32_t bone,
                                                   const Quaternion modelRotation, const float weight)
    {
        const auto parent = skeleton.Bones()[bone].Parent;
        Quaternion parentRotation;
        if (parent >= 0)
        {
            const auto world = WorldMatrices(skeleton, localPose);
            if (!MatrixRotation(world[static_cast<std::size_t>(parent)], parentRotation))
                return false;
        }
        const auto desiredLocal = parent >= 0 ? Multiply(Conjugate(Normalize(parentRotation)), Normalize(modelRotation))
                                              : Normalize(modelRotation);
        localPose[bone].Rotation = Nlerp(localPose[bone].Rotation, desiredLocal, weight);
        return true;
    }

    [[nodiscard]] inline bool ApplyBoneModelRotationDelta(const SkeletonAsset& skeleton,
                                                          const std::span<BoneTransform> localPose,
                                                          const std::uint32_t bone, const Quaternion delta,
                                                          const float weight)
    {
        const auto world = WorldMatrices(skeleton, localPose);
        Quaternion currentRotation;
        if (!MatrixRotation(world[bone], currentRotation))
            return false;
        return SetBoneModelRotation(skeleton, localPose, bone, Multiply(Normalize(delta), Normalize(currentRotation)),
                                    weight);
    }

    [[nodiscard]] inline Vector3 ProjectOntoPlane(const Vector3 value, const Vector3 normal) noexcept
    {
        return Subtract(value, Multiply(normal, Dot(value, normal)));
    }

    [[nodiscard]] inline bool IsDescendantOf(const SkeletonAsset& skeleton, const std::uint32_t descendant,
                                             const std::uint32_t ancestor) noexcept
    {
        if (descendant >= skeleton.Bones().size() || ancestor >= skeleton.Bones().size() || descendant == ancestor)
            return false;
        auto current = skeleton.Bones()[descendant].Parent;
        for (std::size_t depth = 0; current >= 0 && depth < skeleton.Bones().size(); ++depth)
        {
            if (current == static_cast<std::int32_t>(ancestor))
                return true;
            if (static_cast<std::size_t>(current) >= skeleton.Bones().size())
                return false;
            current = skeleton.Bones()[static_cast<std::size_t>(current)].Parent;
        }
        return false;
    }
} // namespace Keire::RiggingDetail
