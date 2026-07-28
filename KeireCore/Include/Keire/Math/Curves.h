#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Math.h"

#include <compare>
#include <cstddef>
#include <span>
#include <vector>

namespace Keire
{
    enum class CurveInterpolation : unsigned char
    {
        Constant,
        Linear,
        Cubic
    };

    struct CurveKey
    {
        float Time = 0.0F;
        float Value = 0.0F;
        float InTangent = 0.0F;
        float OutTangent = 0.0F;
        CurveInterpolation Interpolation = CurveInterpolation::Linear;

        auto operator<=>(const CurveKey&) const noexcept = default;
    };

    class KEIRE_API Curve1D final
    {
      public:
        static constexpr std::size_t MaximumKeys = 4096;

        Curve1D() = default;
        explicit Curve1D(std::vector<CurveKey> keys);

        [[nodiscard]] static Curve1D Constant(float value);
        [[nodiscard]] static Curve1D Linear(float start, float end);

        [[nodiscard]] std::span<const CurveKey> Keys() const noexcept { return m_Keys; }
        [[nodiscard]] float Evaluate(float time) const;
        void SetKeys(std::vector<CurveKey> keys);
        [[nodiscard]] bool operator==(const Curve1D&) const noexcept = default;

      private:
        std::vector<CurveKey> m_Keys;
    };

    enum class GradientInterpolation : unsigned char
    {
        Constant,
        Linear
    };

    struct ColorGradientKey
    {
        float Time = 0.0F;
        Color Value;

        auto operator<=>(const ColorGradientKey&) const noexcept = default;
    };

    class KEIRE_API ColorGradient final
    {
      public:
        static constexpr std::size_t MaximumKeys = 4096;

        ColorGradient() = default;
        explicit ColorGradient(std::vector<ColorGradientKey> keys,
                               GradientInterpolation interpolation = GradientInterpolation::Linear);

        [[nodiscard]] static ColorGradient Constant(Color value);

        [[nodiscard]] std::span<const ColorGradientKey> Keys() const noexcept { return m_Keys; }
        [[nodiscard]] GradientInterpolation Interpolation() const noexcept { return m_Interpolation; }
        [[nodiscard]] Color Evaluate(float time) const;
        void SetKeys(std::vector<ColorGradientKey> keys);
        void SetInterpolation(GradientInterpolation interpolation);
        [[nodiscard]] bool operator==(const ColorGradient&) const noexcept = default;

      private:
        std::vector<ColorGradientKey> m_Keys;
        GradientInterpolation m_Interpolation = GradientInterpolation::Linear;
    };
} // namespace Keire
