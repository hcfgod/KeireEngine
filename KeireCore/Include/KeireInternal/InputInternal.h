#pragma once

#include "Keire/Input/Input.h"

#include <filesystem>
#include <string_view>

namespace Keire::Detail
{
    [[nodiscard]] std::filesystem::path InputBindingProfilePath(const std::filesystem::path& root,
                                                                std::string_view profile);
    [[nodiscard]] InputValue BooleanInputValue(bool value) noexcept;
    [[nodiscard]] InputValue AxisInputValue(float value) noexcept;
    [[nodiscard]] InputValue VectorInputValue(float x, float y) noexcept;
    [[nodiscard]] float NormalizeInputAxis(std::int16_t value) noexcept;
    [[nodiscard]] float NormalizeInputTrigger(std::int16_t value) noexcept;
} // namespace Keire::Detail
