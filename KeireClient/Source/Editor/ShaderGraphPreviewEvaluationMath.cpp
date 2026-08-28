#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorInternal.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    Keire::Vector4 ValueVector(const Keire::ShaderGraphValue& value) noexcept
    {
        if (const auto* scalar = std::get_if<float>(&value))
            return {*scalar, *scalar, *scalar, *scalar};
        if (const auto* vector = std::get_if<Keire::Vector2>(&value))
            return {vector->X, vector->Y, 0.0F, 0.0F};
        if (const auto* vector = std::get_if<Keire::Vector3>(&value))
            return {vector->X, vector->Y, vector->Z, 0.0F};
        if (const auto* vector = std::get_if<Keire::Vector4>(&value))
            return *vector;
        if (const auto* color = std::get_if<Keire::Color>(&value))
            return {color->Red, color->Green, color->Blue, color->Alpha};
        return {};
    }

    Keire::Vector3 Normalize(const Keire::Vector3 value, const Keire::Vector3 fallback) noexcept
    {
        const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
            return fallback;
        const float inverseLength = 1.0F / std::sqrt(lengthSquared);
        return {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
    }

    float Dot(const Keire::Vector3 first, const Keire::Vector3 second) noexcept
    {
        return first.X * second.X + first.Y * second.Y + first.Z * second.Z;
    }

    Keire::Vector3 Cross(const Keire::Vector3 first, const Keire::Vector3 second) noexcept
    {
        return {first.Y * second.Z - first.Z * second.Y, first.Z * second.X - first.X * second.Z,
                first.X * second.Y - first.Y * second.X};
    }

    std::size_t ComponentCount(const Keire::ShaderGraphValueType type) noexcept
    {
        switch (type)
        {
        case Keire::ShaderGraphValueType::Scalar:
            return 1;
        case Keire::ShaderGraphValueType::Vector2:
            return 2;
        case Keire::ShaderGraphValueType::Vector3:
            return 3;
        case Keire::ShaderGraphValueType::Vector4:
        case Keire::ShaderGraphValueType::Color:
            return 4;
        case Keire::ShaderGraphValueType::Texture2D:
        case Keire::ShaderGraphValueType::MaterialAttributes:
        case Keire::ShaderGraphValueType::Bsdf:
            return 0;
        }
        return 0;
    }

    float Component(const Keire::Vector4 value, const std::size_t index) noexcept
    {
        switch (index)
        {
        case 0:
            return value.X;
        case 1:
            return value.Y;
        case 2:
            return value.Z;
        default:
            return value.W;
        }
    }

    void SetComponent(Keire::Vector4& value, const std::size_t index, const float component) noexcept
    {
        switch (index)
        {
        case 0:
            value.X = component;
            break;
        case 1:
            value.Y = component;
            break;
        case 2:
            value.Z = component;
            break;
        default:
            value.W = component;
            break;
        }
    }

    PreviewGraphValue GraphValue(const Keire::ShaderGraphValue& value, const Keire::ShaderGraphValueType type,
                                 const Keire::ShaderTextureSemantic semantic) noexcept
    {
        PreviewGraphValue result{.Data = ValueVector(value), .Type = type, .TextureSemantic = semantic};
        if (const auto* texture = std::get_if<Keire::AssetId>(&value))
            result.Texture = *texture;
        if (type == Keire::ShaderGraphValueType::MaterialAttributes || type == Keire::ShaderGraphValueType::Bsdf)
            result.Surface.emplace();
        return result;
    }

    float SafeDivide(const float first, const float second) noexcept
    {
        const float divisor = std::abs(second) >= 1.0e-5F ? second : (second < 0.0F ? -1.0e-5F : 1.0e-5F);
        return first / divisor;
    }

    float Fraction(const float value) noexcept { return value - std::floor(value); }

    float MaterialHash(const Keire::Vector2 value) noexcept
    {
        const Keire::Vector2 wrapped{Fraction(value.X * 0.1031F), Fraction(value.Y * 0.103F)};
        const float mixed = wrapped.X * (wrapped.Y + 33.33F) + wrapped.Y * (wrapped.X + 33.33F);
        return Fraction((wrapped.X + wrapped.Y) * mixed);
    }

    float MaterialValueNoise(const Keire::Vector2 position) noexcept
    {
        const Keire::Vector2 cell{std::floor(position.X), std::floor(position.Y)};
        const Keire::Vector2 local{Fraction(position.X), Fraction(position.Y)};
        const Keire::Vector2 blend{local.X * local.X * (3.0F - 2.0F * local.X),
                                   local.Y * local.Y * (3.0F - 2.0F * local.Y)};
        const float bottom = std::lerp(MaterialHash(cell), MaterialHash({cell.X + 1.0F, cell.Y}), blend.X);
        const float top =
            std::lerp(MaterialHash({cell.X, cell.Y + 1.0F}), MaterialHash({cell.X + 1.0F, cell.Y + 1.0F}), blend.X);
        return std::lerp(bottom, top, blend.Y);
    }

    float MaterialNoise(const Keire::Vector2 uv, const float scale, const float detail) noexcept
    {
        Keire::Vector2 position{uv.X * std::max(std::abs(scale), 1.0e-4F), uv.Y * std::max(std::abs(scale), 1.0e-4F)};
        const float persistence = std::clamp(detail, 0.0F, 1.0F) * 0.5F;
        float amplitude = 0.5F;
        float value = MaterialValueNoise(position) * amplitude;
        float normalization = amplitude;
        for (std::size_t octave = 1; octave < 4; ++octave)
        {
            position = {position.X * 2.0F + 17.0F, position.Y * 2.0F + 29.0F};
            amplitude *= persistence;
            value += MaterialValueNoise(position) * amplitude;
            normalization += amplitude;
        }
        return value / std::max(normalization, 1.0e-5F);
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
