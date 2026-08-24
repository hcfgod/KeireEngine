#pragma once

#include "Keire/Input/Input.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    [[nodiscard]] std::filesystem::path InputBindingProfilePath(const std::filesystem::path& root,
                                                                std::string_view profile);
    [[nodiscard]] InputValue BooleanInputValue(bool value) noexcept;
    [[nodiscard]] InputValue AxisInputValue(float value) noexcept;
    [[nodiscard]] InputValue VectorInputValue(float x, float y) noexcept;
    [[nodiscard]] float NormalizeInputAxis(std::int16_t value) noexcept;
    [[nodiscard]] float NormalizeInputTrigger(std::int16_t value) noexcept;
    [[nodiscard]] std::string KeyboardInputPath(std::int32_t scancode);
    [[nodiscard]] std::string GamepadButtonInputPath(std::int32_t button);
    [[nodiscard]] std::string_view GamepadAxisInputPath(std::int32_t axis) noexcept;
    [[nodiscard]] InputValue GamepadAxisInputValue(std::int32_t axis, std::int16_t rawValue,
                                                   InputValue current) noexcept;
    [[nodiscard]] double InputParameterValue(const std::vector<InputParameter>& parameters, std::string_view name,
                                             double fallback) noexcept;
    [[nodiscard]] InputValue ApplyInputProcessors(InputValue value,
                                                  const std::vector<InputBehaviorDefinition>& processors);
} // namespace Keire::Detail
