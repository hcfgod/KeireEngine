#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Keire
{
    namespace Detail
    {
        [[nodiscard]] std::string VfxAssetIdText(AssetId id);
        [[nodiscard]] AssetId ParseVfxAssetId(const nlohmann::json& object, const char* key);
        [[nodiscard]] AssetId DerivedVfxGraphId(AssetId source, std::uint64_t salt) noexcept;
        [[nodiscard]] nlohmann::json EncodeVfxVector2(Vector2 value);
        [[nodiscard]] nlohmann::json EncodeVfxVector3(Vector3 value);
        [[nodiscard]] nlohmann::json EncodeVfxVector4(Vector4 value);
        [[nodiscard]] nlohmann::json EncodeVfxQuaternion(Quaternion value);
        [[nodiscard]] nlohmann::json EncodeVfxMatrix(const Matrix4& value);
        [[nodiscard]] Vector2 DecodeVfxVector2(const nlohmann::json& value);
        [[nodiscard]] Vector3 DecodeVfxVector3(const nlohmann::json& value);
        [[nodiscard]] Vector4 DecodeVfxVector4(const nlohmann::json& value);
        [[nodiscard]] Quaternion DecodeVfxQuaternion(const nlohmann::json& value);
        [[nodiscard]] Matrix4 DecodeVfxMatrix(const nlohmann::json& value);
        [[nodiscard]] nlohmann::json EncodeVfxColor(Color value);
        [[nodiscard]] Color DecodeVfxColor(const nlohmann::json& value);
        [[nodiscard]] std::string_view VfxKillShapeModeName(VfxKillShapeMode mode);
        [[nodiscard]] VfxKillShapeMode ParseVfxKillShapeMode(std::string_view value);
        [[nodiscard]] std::string_view VfxCurveInterpolationName(CurveInterpolation interpolation);
        [[nodiscard]] CurveInterpolation ParseVfxCurveInterpolation(std::string_view value);
        [[nodiscard]] nlohmann::json EncodeVfxCurve(const Curve1D& curve);
        [[nodiscard]] Curve1D DecodeVfxCurve(const nlohmann::json& value);
        [[nodiscard]] std::string_view VfxGradientInterpolationName(GradientInterpolation interpolation);
        [[nodiscard]] GradientInterpolation ParseVfxGradientInterpolation(std::string_view value);
        [[nodiscard]] nlohmann::json EncodeVfxGradient(const ColorGradient& gradient);
        [[nodiscard]] ColorGradient DecodeVfxGradient(const nlohmann::json& value);
    } // namespace Detail
} // namespace Keire
