#pragma once

#include "Keire/Api.h"

#include <array>
#include <compare>

namespace Keire
{
    struct Vector2
    {
        float X = 0.0F;
        float Y = 0.0F;
        auto operator<=>(const Vector2&) const noexcept = default;
    };

    struct Vector3
    {
        float X = 0.0F;
        float Y = 0.0F;
        float Z = 0.0F;
        auto operator<=>(const Vector3&) const noexcept = default;
    };

    struct Vector4
    {
        float X = 0.0F;
        float Y = 0.0F;
        float Z = 0.0F;
        float W = 0.0F;
        auto operator<=>(const Vector4&) const noexcept = default;
    };

    struct Quaternion
    {
        float X = 0.0F;
        float Y = 0.0F;
        float Z = 0.0F;
        float W = 1.0F;
        auto operator<=>(const Quaternion&) const noexcept = default;
    };

    struct Color
    {
        float Red = 1.0F;
        float Green = 1.0F;
        float Blue = 1.0F;
        float Alpha = 1.0F;
        auto operator<=>(const Color&) const noexcept = default;
    };

    struct Matrix4
    {
        std::array<float, 16> Elements{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                                       0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
        auto operator<=>(const Matrix4&) const noexcept = default;
    };

    namespace Math
    {
        [[nodiscard]] KEIRE_API bool IsFinite(Vector2 value) noexcept;
        [[nodiscard]] KEIRE_API bool IsFinite(Vector3 value) noexcept;
        [[nodiscard]] KEIRE_API bool IsFinite(Vector4 value) noexcept;
        [[nodiscard]] KEIRE_API bool IsFinite(Quaternion value) noexcept;
        [[nodiscard]] KEIRE_API bool IsFinite(Color value) noexcept;
        [[nodiscard]] KEIRE_API bool IsFinite(const Matrix4& value) noexcept;
        [[nodiscard]] KEIRE_API float Length(Quaternion value) noexcept;
        [[nodiscard]] KEIRE_API Quaternion Normalize(Quaternion value);
        [[nodiscard]] KEIRE_API Quaternion EulerDegreesToQuaternion(Vector3 degrees);
        [[nodiscard]] KEIRE_API Vector3 QuaternionToEulerDegrees(Quaternion value);
        [[nodiscard]] KEIRE_API Matrix4 ComposeTransform(Vector3 position, Quaternion rotation, Vector3 scale);
        [[nodiscard]] KEIRE_API bool DecomposeTransform(const Matrix4& matrix, Vector3& position, Quaternion& rotation,
                                                        Vector3& scale) noexcept;
        [[nodiscard]] KEIRE_API Matrix4 Multiply(const Matrix4& left, const Matrix4& right) noexcept;
        [[nodiscard]] KEIRE_API Matrix4 Inverse(const Matrix4& value);
        [[nodiscard]] KEIRE_API Vector3 TransformPoint(const Matrix4& matrix, Vector3 point) noexcept;
        [[nodiscard]] KEIRE_API Vector3 TransformDirection(const Matrix4& matrix, Vector3 direction) noexcept;
        [[nodiscard]] KEIRE_API Matrix4 LookAt(Vector3 eye, Vector3 target, Vector3 up);
        [[nodiscard]] KEIRE_API Matrix4 Perspective(float verticalFieldOfViewDegrees, float aspectRatio,
                                                    float nearPlane, float farPlane);
        [[nodiscard]] KEIRE_API Matrix4 Orthographic(float verticalSize, float aspectRatio, float nearPlane,
                                                     float farPlane);
    } // namespace Math
} // namespace Keire
