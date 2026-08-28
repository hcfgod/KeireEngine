#include "Keire/Math/Curves.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <string>

namespace Keire
{
    namespace
    {
        template <typename Key>
        void ValidateKeys(const std::span<const Key> keys, const std::size_t maximum, const char* name)
        {
            if (keys.size() > maximum)
                throw std::length_error(std::string(name) + " exceeds its key limit.");
            for (std::size_t index = 0; index < keys.size(); ++index)
            {
                if (!std::isfinite(keys[index].Time) || (index > 0 && keys[index - 1].Time >= keys[index].Time))
                    throw std::invalid_argument(std::string(name) + " key times must be finite and strictly ordered.");
            }
        }

        void ValidateCurveKeys(const std::span<const CurveKey> keys)
        {
            ValidateKeys(keys, Curve1D::MaximumKeys, "Curve");
            for (const auto& key : keys)
            {
                if (!std::isfinite(key.Value) || !std::isfinite(key.InTangent) || !std::isfinite(key.OutTangent))
                    throw std::invalid_argument("Curve keys must contain finite values and tangents.");
                if (key.Interpolation > CurveInterpolation::Cubic)
                    throw std::invalid_argument("Curve key interpolation is invalid.");
            }
        }

        void ValidateGradientKeys(const std::span<const ColorGradientKey> keys)
        {
            ValidateKeys(keys, ColorGradient::MaximumKeys, "Gradient");
            for (const auto& key : keys)
                if (!Math::IsFinite(key.Value))
                    throw std::invalid_argument("Gradient keys must contain finite colors.");
        }

        [[nodiscard]] float Hermite(const CurveKey& left, const CurveKey& right, const float alpha)
        {
            const auto span = right.Time - left.Time;
            const auto alphaSquared = alpha * alpha;
            const auto alphaCubed = alphaSquared * alpha;
            const auto h00 = 2.0F * alphaCubed - 3.0F * alphaSquared + 1.0F;
            const auto h10 = alphaCubed - 2.0F * alphaSquared + alpha;
            const auto h01 = -2.0F * alphaCubed + 3.0F * alphaSquared;
            const auto h11 = alphaCubed - alphaSquared;
            return h00 * left.Value + h10 * span * left.OutTangent + h01 * right.Value + h11 * span * right.InTangent;
        }

    } // namespace

    Curve1D::Curve1D(std::vector<CurveKey> keys) { SetKeys(std::move(keys)); }

    Curve1D Curve1D::Constant(const float value)
    {
        if (!std::isfinite(value))
            throw std::invalid_argument("A constant curve value must be finite.");
        return Curve1D({{0.0F, value}});
    }

    Curve1D Curve1D::Linear(const float start, const float end)
    {
        if (!std::isfinite(start) || !std::isfinite(end))
            throw std::invalid_argument("Linear curve values must be finite.");
        return Curve1D({{0.0F, start}, {1.0F, end}});
    }

    float Curve1D::Evaluate(const float time) const
    {
        if (!std::isfinite(time))
            throw std::invalid_argument("Curve evaluation time must be finite.");
        if (m_Keys.empty())
            return 0.0F;
        if (time <= m_Keys.front().Time)
            return m_Keys.front().Value;
        if (time >= m_Keys.back().Time)
            return m_Keys.back().Value;

        const auto right = std::ranges::upper_bound(m_Keys, time, {}, &CurveKey::Time);
        const auto left = std::prev(right);
        const auto alpha = (time - left->Time) / (right->Time - left->Time);
        switch (left->Interpolation)
        {
        case CurveInterpolation::Constant:
            return left->Value;
        case CurveInterpolation::Linear:
            return left->Value + (right->Value - left->Value) * alpha;
        case CurveInterpolation::Cubic:
            return Hermite(*left, *right, alpha);
        }
        throw std::logic_error("Curve interpolation is invalid.");
    }

    void Curve1D::SetKeys(std::vector<CurveKey> keys)
    {
        ValidateCurveKeys(keys);
        m_Keys = std::move(keys);
    }

    ColorGradient::ColorGradient(std::vector<ColorGradientKey> keys, const GradientInterpolation interpolation)
    {
        SetInterpolation(interpolation);
        SetKeys(std::move(keys));
    }

    ColorGradient ColorGradient::Constant(const Color value)
    {
        if (!Math::IsFinite(value))
            throw std::invalid_argument("A constant gradient color must be finite.");
        return ColorGradient({{0.0F, value}});
    }

    Color ColorGradient::Evaluate(const float time) const
    {
        if (!std::isfinite(time))
            throw std::invalid_argument("Gradient evaluation time must be finite.");
        if (m_Keys.empty())
            return {};
        if (time <= m_Keys.front().Time)
            return m_Keys.front().Value;
        if (time >= m_Keys.back().Time)
            return m_Keys.back().Value;

        const auto right = std::ranges::upper_bound(m_Keys, time, {}, &ColorGradientKey::Time);
        const auto left = std::prev(right);
        if (m_Interpolation == GradientInterpolation::Constant)
            return left->Value;
        const auto alpha = (time - left->Time) / (right->Time - left->Time);
        return Math::Lerp(left->Value, right->Value, alpha);
    }

    void ColorGradient::SetKeys(std::vector<ColorGradientKey> keys)
    {
        ValidateGradientKeys(keys);
        m_Keys = std::move(keys);
    }

    void ColorGradient::SetInterpolation(const GradientInterpolation interpolation)
    {
        if (interpolation > GradientInterpolation::Linear)
            throw std::invalid_argument("Gradient interpolation is invalid.");
        m_Interpolation = interpolation;
    }
} // namespace Keire
