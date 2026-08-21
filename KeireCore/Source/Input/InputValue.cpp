#include "Keire/Input/Input.h"
#include "KeireInternal/InputInternal.h"

#include <algorithm>
#include <cmath>

namespace Keire
{
    bool InputValue::AsBoolean(const float threshold) const noexcept { return Magnitude() >= threshold; }

    float InputValue::Magnitude() const noexcept
    {
        return Type == InputValueType::Axis2D ? std::sqrt(X * X + Y * Y) : std::abs(X);
    }

    bool InputValue::NearlyEquals(const InputValue& other, const float epsilon) const noexcept
    {
        return Type == other.Type && std::abs(X - other.X) <= epsilon && std::abs(Y - other.Y) <= epsilon;
    }

    InputValue Detail::BooleanInputValue(const bool value) noexcept
    {
        return {InputValueType::Boolean, value ? 1.0F : 0.0F, 0.0F};
    }

    InputValue Detail::AxisInputValue(const float value) noexcept { return {InputValueType::Axis1D, value, 0.0F}; }

    InputValue Detail::VectorInputValue(const float x, const float y) noexcept
    {
        return {InputValueType::Axis2D, x, y};
    }

    float Detail::NormalizeInputAxis(const std::int16_t value) noexcept
    {
        return value < 0 ? static_cast<float>(value) / 32768.0F : static_cast<float>(value) / 32767.0F;
    }

    float Detail::NormalizeInputTrigger(const std::int16_t value) noexcept
    {
        return std::clamp((NormalizeInputAxis(value) + 1.0F) * 0.5F, 0.0F, 1.0F);
    }
} // namespace Keire
