#include "Keire/Math/Math.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>

namespace Keire::Math
{
    namespace
    {
        [[nodiscard]] glm::quat ToGlm(const Quaternion value) noexcept { return {value.W, value.X, value.Y, value.Z}; }

        [[nodiscard]] Quaternion FromGlm(const glm::quat value) noexcept
        {
            return {value.x, value.y, value.z, value.w};
        }

        [[nodiscard]] glm::mat4 ToGlm(const Matrix4& value) noexcept
        {
            glm::mat4 result(1.0F);
            for (std::size_t column = 0; column < 4; ++column)
                for (std::size_t row = 0; row < 4; ++row)
                    result[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)] =
                        value.Elements[column * 4 + row];
            return result;
        }

        [[nodiscard]] Matrix4 FromGlm(const glm::mat4& value) noexcept
        {
            Matrix4 result;
            for (std::size_t column = 0; column < 4; ++column)
                for (std::size_t row = 0; row < 4; ++row)
                    result.Elements[column * 4 + row] =
                        value[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)];
            return result;
        }
    } // namespace

    bool IsFinite(const Vector2 value) noexcept { return std::isfinite(value.X) && std::isfinite(value.Y); }
    bool IsFinite(const Vector3 value) noexcept
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }
    bool IsFinite(const Vector4 value) noexcept
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z) && std::isfinite(value.W);
    }
    bool IsFinite(const Quaternion value) noexcept
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z) && std::isfinite(value.W);
    }
    bool IsFinite(const Color value) noexcept
    {
        return std::isfinite(value.Red) && std::isfinite(value.Green) && std::isfinite(value.Blue) &&
               std::isfinite(value.Alpha);
    }
    bool IsFinite(const Matrix4& value) noexcept
    {
        return std::ranges::all_of(value.Elements, [](const float item) { return std::isfinite(item); });
    }
    float Length(const Quaternion value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z + value.W * value.W);
    }
    Quaternion Normalize(const Quaternion value)
    {
        if (!IsFinite(value) || Length(value) <= 0.000001F)
            throw std::invalid_argument("Quaternion must be finite and nonzero.");
        return FromGlm(glm::normalize(ToGlm(value)));
    }
    Quaternion EulerDegreesToQuaternion(const Vector3 degrees)
    {
        if (!IsFinite(degrees))
            throw std::invalid_argument("Euler angles must be finite.");
        return FromGlm(glm::normalize(glm::quat(glm::radians(glm::vec3(degrees.X, degrees.Y, degrees.Z)))));
    }
    Vector3 QuaternionToEulerDegrees(const Quaternion value)
    {
        const auto euler = glm::degrees(glm::eulerAngles(ToGlm(Normalize(value))));
        return {euler.x, euler.y, euler.z};
    }
    Matrix4 ComposeTransform(const Vector3 position, const Quaternion rotation, const Vector3 scale)
    {
        if (!IsFinite(position) || !IsFinite(scale))
            throw std::invalid_argument("Transform values must be finite.");
        const auto translation = glm::translate(glm::mat4(1.0F), {position.X, position.Y, position.Z});
        const auto orientation = glm::toMat4(ToGlm(Normalize(rotation)));
        return FromGlm(glm::scale(translation * orientation, {scale.X, scale.Y, scale.Z}));
    }
    bool DecomposeTransform(const Matrix4& matrix, Vector3& position, Quaternion& rotation, Vector3& scale) noexcept
    {
        if (!IsFinite(matrix))
            return false;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::vec3 scaling;
        glm::quat orientation;
        if (!glm::decompose(ToGlm(matrix), scaling, orientation, translation, skew, perspective))
            return false;
        position = {translation.x, translation.y, translation.z};
        rotation = FromGlm(glm::normalize(orientation));
        scale = {scaling.x, scaling.y, scaling.z};
        return IsFinite(position) && IsFinite(rotation) && IsFinite(scale);
    }
    Matrix4 Multiply(const Matrix4& left, const Matrix4& right) noexcept { return FromGlm(ToGlm(left) * ToGlm(right)); }
    Matrix4 Inverse(const Matrix4& value)
    {
        if (!IsFinite(value))
            throw std::invalid_argument("Matrix is singular and cannot be inverted.");
        const auto matrix = ToGlm(value);
        const float determinant = glm::determinant(matrix);
        if (!std::isfinite(determinant) || determinant == 0.0F)
            throw std::invalid_argument("Matrix is singular and cannot be inverted.");
        const auto inverse = FromGlm(glm::inverse(matrix));
        if (!IsFinite(inverse))
            throw std::invalid_argument("Matrix is singular and cannot be inverted.");
        return inverse;
    }
    Vector3 TransformPoint(const Matrix4& matrix, const Vector3 point) noexcept
    {
        const auto result = ToGlm(matrix) * glm::vec4(point.X, point.Y, point.Z, 1.0F);
        return {result.x, result.y, result.z};
    }

    Vector3 TransformDirection(const Matrix4& matrix, const Vector3 direction) noexcept
    {
        const auto result = ToGlm(matrix) * glm::vec4(direction.X, direction.Y, direction.Z, 0.0F);
        return {result.x, result.y, result.z};
    }

    Matrix4 LookAt(const Vector3 eye, const Vector3 target, const Vector3 up)
    {
        if (!IsFinite(eye) || !IsFinite(target) || !IsFinite(up))
            throw std::invalid_argument("Look-at vectors must be finite.");
        const glm::vec3 glmEye(eye.X, eye.Y, eye.Z);
        const glm::vec3 glmTarget(target.X, target.Y, target.Z);
        const glm::vec3 glmUp(up.X, up.Y, up.Z);
        if (glm::length(glmTarget - glmEye) <= 0.000001F || glm::length(glmUp) <= 0.000001F)
            throw std::invalid_argument("Look-at eye, target, and up vectors must define a valid view.");
        return FromGlm(glm::lookAtLH(glmEye, glmTarget, glm::normalize(glmUp)));
    }

    Matrix4 Perspective(const float verticalFieldOfViewDegrees, const float aspectRatio, const float nearPlane,
                        const float farPlane)
    {
        if (!std::isfinite(verticalFieldOfViewDegrees) || verticalFieldOfViewDegrees <= 1.0F ||
            verticalFieldOfViewDegrees >= 179.0F || !std::isfinite(aspectRatio) || aspectRatio <= 0.0F ||
            !std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0.0F || farPlane <= nearPlane)
            throw std::invalid_argument("Perspective projection values are invalid.");
        return FromGlm(
            glm::perspectiveLH_ZO(glm::radians(verticalFieldOfViewDegrees), aspectRatio, nearPlane, farPlane));
    }

    Matrix4 Orthographic(const float verticalSize, const float aspectRatio, const float nearPlane, const float farPlane)
    {
        if (!std::isfinite(verticalSize) || verticalSize <= 0.0F || !std::isfinite(aspectRatio) ||
            aspectRatio <= 0.0F || !std::isfinite(nearPlane) || !std::isfinite(farPlane) || farPlane <= nearPlane)
            throw std::invalid_argument("Orthographic projection values are invalid.");
        const float halfHeight = verticalSize * 0.5F;
        const float halfWidth = halfHeight * aspectRatio;
        return FromGlm(glm::orthoLH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane));
    }
} // namespace Keire::Math
