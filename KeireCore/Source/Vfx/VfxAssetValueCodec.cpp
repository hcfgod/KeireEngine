#include "KeireInternal/Vfx/VfxAssetValueCodec.h"

#include <nlohmann/json.hpp>

#include <bit>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace Detail
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::string VfxAssetIdText(const AssetId id) { return id ? id.ToString() : std::string{}; }

        [[nodiscard]] AssetId ParseVfxAssetId(const Json& object, const char* key)
        {
            const auto text = object.value(key, std::string{});
            return text.empty() ? AssetId{} : AssetId::Parse(text);
        }

        [[nodiscard]] AssetId DerivedVfxGraphId(const AssetId source, const std::uint64_t salt) noexcept
        {
            auto high = source.High() ^ 0x4752415048564658ULL;
            auto low = source.Low() ^ salt;
            high = (high & 0xffffffffffff0fffULL) | 0x0000000000005000ULL;
            low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
            return AssetId(high, low);
        }

        [[nodiscard]] Json EncodeVfxVector3(const Vector3 value) { return Json::array({value.X, value.Y, value.Z}); }

        [[nodiscard]] Json EncodeVfxVector2(const Vector2 value) { return Json::array({value.X, value.Y}); }

        [[nodiscard]] Json EncodeVfxVector4(const Vector4 value)
        {
            return Json::array({value.X, value.Y, value.Z, value.W});
        }

        [[nodiscard]] Json EncodeVfxQuaternion(const Quaternion value)
        {
            return Json::array({value.X, value.Y, value.Z, value.W});
        }

        [[nodiscard]] Json EncodeVfxMatrix(const Matrix4& value)
        {
            auto result = Json::array();
            for (const auto element : value.Elements)
                result.push_back(element);
            return result;
        }

        [[nodiscard]] Vector2 DecodeVfxVector2(const Json& value)
        {
            if (!value.is_array() || value.size() != 2)
                throw std::runtime_error("VFX Vector2 values must contain exactly two scalars.");
            return {value.at(0).get<float>(), value.at(1).get<float>()};
        }

        [[nodiscard]] Vector3 DecodeVfxVector3(const Json& value)
        {
            if (!value.is_array() || value.size() != 3)
                throw std::runtime_error("VFX vector values must contain exactly three scalars.");
            return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
        }

        [[nodiscard]] Vector4 DecodeVfxVector4(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::runtime_error("VFX Vector4 values must contain exactly four scalars.");
            return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(),
                    value.at(3).get<float>()};
        }

        [[nodiscard]] Quaternion DecodeVfxQuaternion(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::runtime_error("VFX quaternion values must contain exactly four scalars.");
            return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(),
                    value.at(3).get<float>()};
        }

        [[nodiscard]] Matrix4 DecodeVfxMatrix(const Json& value)
        {
            if (!value.is_array() || value.size() != 16)
                throw std::runtime_error("VFX matrix values must contain exactly sixteen scalars.");
            Matrix4 result;
            for (std::size_t index = 0; index < result.Elements.size(); ++index)
                result.Elements[index] = value.at(index).get<float>();
            return result;
        }

        [[nodiscard]] Json EncodeVfxColor(const Color value)
        {
            return Json::array({value.Red, value.Green, value.Blue, value.Alpha});
        }

        [[nodiscard]] Color DecodeVfxColor(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::runtime_error("VFX color values must contain exactly four scalars.");
            return {value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>(),
                    value.at(3).get<float>()};
        }

        [[nodiscard]] std::string_view VfxKillShapeModeName(const VfxKillShapeMode mode)
        {
            switch (mode)
            {
            case VfxKillShapeMode::Solid:
                return "solid";
            case VfxKillShapeMode::Inverted:
                return "inverted";
            }
            throw std::invalid_argument("VFX kill-shape mode is unsupported.");
        }

        [[nodiscard]] VfxKillShapeMode ParseVfxKillShapeMode(const std::string_view value)
        {
            if (value == "solid")
                return VfxKillShapeMode::Solid;
            if (value == "inverted")
                return VfxKillShapeMode::Inverted;
            throw std::runtime_error("VFX kill-shape mode is unsupported.");
        }

        [[nodiscard]] std::string_view VfxCurveInterpolationName(const CurveInterpolation interpolation)
        {
            switch (interpolation)
            {
            case CurveInterpolation::Constant:
                return "constant";
            case CurveInterpolation::Linear:
                return "linear";
            case CurveInterpolation::Cubic:
                return "cubic";
            }
            throw std::invalid_argument("VFX curve interpolation is unsupported.");
        }

        [[nodiscard]] CurveInterpolation ParseVfxCurveInterpolation(const std::string_view value)
        {
            if (value == "constant")
                return CurveInterpolation::Constant;
            if (value == "linear")
                return CurveInterpolation::Linear;
            if (value == "cubic")
                return CurveInterpolation::Cubic;
            throw std::runtime_error("VFX curve interpolation is unsupported.");
        }

        [[nodiscard]] Json EncodeVfxCurve(const Curve1D& curve)
        {
            auto result = Json::array();
            for (const auto& key : curve.Keys())
            {
                result.push_back({{"time", key.Time},
                                  {"value", key.Value},
                                  {"inTangent", key.InTangent},
                                  {"outTangent", key.OutTangent},
                                  {"interpolation", VfxCurveInterpolationName(key.Interpolation)}});
            }
            return result;
        }

        [[nodiscard]] Curve1D DecodeVfxCurve(const Json& value)
        {
            if (!value.is_array() || value.size() > Curve1D::MaximumKeys)
                throw std::runtime_error("VFX curve is not an array or exceeds its key limit.");
            std::vector<CurveKey> keys;
            keys.reserve(value.size());
            for (const auto& key : value)
            {
                keys.push_back({key.at("time").get<float>(), key.at("value").get<float>(), key.value("inTangent", 0.0F),
                                key.value("outTangent", 0.0F),
                                ParseVfxCurveInterpolation(key.value("interpolation", std::string("linear")))});
            }
            return Curve1D(std::move(keys));
        }

        [[nodiscard]] std::string_view VfxGradientInterpolationName(const GradientInterpolation interpolation)
        {
            switch (interpolation)
            {
            case GradientInterpolation::Constant:
                return "constant";
            case GradientInterpolation::Linear:
                return "linear";
            }
            throw std::invalid_argument("VFX gradient interpolation is unsupported.");
        }

        [[nodiscard]] GradientInterpolation ParseVfxGradientInterpolation(const std::string_view value)
        {
            if (value == "constant")
                return GradientInterpolation::Constant;
            if (value == "linear")
                return GradientInterpolation::Linear;
            throw std::runtime_error("VFX gradient interpolation is unsupported.");
        }

        [[nodiscard]] Json EncodeVfxGradient(const ColorGradient& gradient)
        {
            auto keys = Json::array();
            for (const auto& key : gradient.Keys())
                keys.push_back({{"time", key.Time}, {"color", EncodeVfxColor(key.Value)}});
            return {{"interpolation", VfxGradientInterpolationName(gradient.Interpolation())},
                    {"keys", std::move(keys)}};
        }

        [[nodiscard]] ColorGradient DecodeVfxGradient(const Json& value)
        {
            if (!value.is_object() || !value.contains("keys") || !value.at("keys").is_array() ||
                value.at("keys").size() > ColorGradient::MaximumKeys)
            {
                throw std::runtime_error("VFX gradient is malformed or exceeds its key limit.");
            }
            std::vector<ColorGradientKey> keys;
            keys.reserve(value.at("keys").size());
            for (const auto& key : value.at("keys"))
                keys.push_back({key.at("time").get<float>(), DecodeVfxColor(key.at("color"))});
            return ColorGradient(std::move(keys),
                                 ParseVfxGradientInterpolation(value.value("interpolation", std::string("linear"))));
        }

    } // namespace Detail
} // namespace Keire
