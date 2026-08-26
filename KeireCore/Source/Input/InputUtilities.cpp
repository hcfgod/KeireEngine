#include "KeireInternal/InputInternal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    std::filesystem::path InputBindingProfilePath(const std::filesystem::path& root, const std::string_view profile)
    {
        if (profile.empty() || profile.size() > 128 ||
            std::ranges::any_of(profile, [](const unsigned char value)
                                { return !std::isalnum(value) && value != '-' && value != '_'; }))
        {
            throw std::invalid_argument("Input binding profile names may contain only letters, digits, '-' and '_'.");
        }
        return root / (std::string(profile) + ".json");
    }

    std::string KeyboardInputPath(const std::int32_t scancodeValue)
    {
        const auto scancode = static_cast<SDL_Scancode>(scancodeValue);
        if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
            return "<Keyboard>/" + std::string(1, static_cast<char>('a' + scancode - SDL_SCANCODE_A));
        if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
            return "<Keyboard>/digit" + std::to_string(1 + scancode - SDL_SCANCODE_1);
        if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
            return "<Keyboard>/f" + std::to_string(1 + scancode - SDL_SCANCODE_F1);
        if (scancode >= SDL_SCANCODE_F13 && scancode <= SDL_SCANCODE_F24)
            return "<Keyboard>/f" + std::to_string(13 + scancode - SDL_SCANCODE_F13);
        switch (scancode)
        {
        case SDL_SCANCODE_0:
            return "<Keyboard>/digit0";
        case SDL_SCANCODE_SPACE:
            return "<Keyboard>/space";
        case SDL_SCANCODE_RETURN:
            return "<Keyboard>/enter";
        case SDL_SCANCODE_KP_ENTER:
            return "<Keyboard>/numpadEnter";
        case SDL_SCANCODE_ESCAPE:
            return "<Keyboard>/escape";
        case SDL_SCANCODE_UP:
            return "<Keyboard>/upArrow";
        case SDL_SCANCODE_DOWN:
            return "<Keyboard>/downArrow";
        case SDL_SCANCODE_LEFT:
            return "<Keyboard>/leftArrow";
        case SDL_SCANCODE_RIGHT:
            return "<Keyboard>/rightArrow";
        case SDL_SCANCODE_TAB:
            return "<Keyboard>/tab";
        case SDL_SCANCODE_BACKSPACE:
            return "<Keyboard>/backspace";
        case SDL_SCANCODE_CAPSLOCK:
            return "<Keyboard>/capsLock";
        case SDL_SCANCODE_LSHIFT:
            return "<Keyboard>/leftShift";
        case SDL_SCANCODE_RSHIFT:
            return "<Keyboard>/rightShift";
        case SDL_SCANCODE_LCTRL:
            return "<Keyboard>/leftCtrl";
        case SDL_SCANCODE_RCTRL:
            return "<Keyboard>/rightCtrl";
        case SDL_SCANCODE_LALT:
            return "<Keyboard>/leftAlt";
        case SDL_SCANCODE_RALT:
            return "<Keyboard>/rightAlt";
        case SDL_SCANCODE_LGUI:
            return "<Keyboard>/leftMeta";
        case SDL_SCANCODE_RGUI:
            return "<Keyboard>/rightMeta";
        case SDL_SCANCODE_INSERT:
            return "<Keyboard>/insert";
        case SDL_SCANCODE_DELETE:
            return "<Keyboard>/delete";
        case SDL_SCANCODE_HOME:
            return "<Keyboard>/home";
        case SDL_SCANCODE_END:
            return "<Keyboard>/end";
        case SDL_SCANCODE_PAGEUP:
            return "<Keyboard>/pageUp";
        case SDL_SCANCODE_PAGEDOWN:
            return "<Keyboard>/pageDown";
        case SDL_SCANCODE_PRINTSCREEN:
            return "<Keyboard>/printScreen";
        case SDL_SCANCODE_SCROLLLOCK:
            return "<Keyboard>/scrollLock";
        case SDL_SCANCODE_PAUSE:
            return "<Keyboard>/pause";
        case SDL_SCANCODE_MINUS:
            return "<Keyboard>/minus";
        case SDL_SCANCODE_EQUALS:
            return "<Keyboard>/equals";
        case SDL_SCANCODE_LEFTBRACKET:
            return "<Keyboard>/leftBracket";
        case SDL_SCANCODE_RIGHTBRACKET:
            return "<Keyboard>/rightBracket";
        case SDL_SCANCODE_BACKSLASH:
            return "<Keyboard>/backslash";
        case SDL_SCANCODE_SEMICOLON:
            return "<Keyboard>/semicolon";
        case SDL_SCANCODE_APOSTROPHE:
            return "<Keyboard>/quote";
        case SDL_SCANCODE_GRAVE:
            return "<Keyboard>/backquote";
        case SDL_SCANCODE_COMMA:
            return "<Keyboard>/comma";
        case SDL_SCANCODE_PERIOD:
            return "<Keyboard>/period";
        case SDL_SCANCODE_SLASH:
            return "<Keyboard>/slash";
        case SDL_SCANCODE_NUMLOCKCLEAR:
            return "<Keyboard>/numLock";
        case SDL_SCANCODE_KP_0:
            return "<Keyboard>/numpad0";
        case SDL_SCANCODE_KP_1:
        case SDL_SCANCODE_KP_2:
        case SDL_SCANCODE_KP_3:
        case SDL_SCANCODE_KP_4:
        case SDL_SCANCODE_KP_5:
        case SDL_SCANCODE_KP_6:
        case SDL_SCANCODE_KP_7:
        case SDL_SCANCODE_KP_8:
        case SDL_SCANCODE_KP_9:
            return "<Keyboard>/numpad" + std::to_string(scancode - SDL_SCANCODE_KP_1 + 1);
        case SDL_SCANCODE_KP_PLUS:
            return "<Keyboard>/numpadPlus";
        case SDL_SCANCODE_KP_MINUS:
            return "<Keyboard>/numpadMinus";
        case SDL_SCANCODE_KP_MULTIPLY:
            return "<Keyboard>/numpadMultiply";
        case SDL_SCANCODE_KP_DIVIDE:
            return "<Keyboard>/numpadDivide";
        case SDL_SCANCODE_KP_DECIMAL:
            return "<Keyboard>/numpadPeriod";
        default:
            return {};
        }
    }

    std::string GamepadButtonInputPath(const std::int32_t buttonValue)
    {
        switch (static_cast<SDL_GamepadButton>(buttonValue))
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return "<Gamepad>/buttonSouth";
        case SDL_GAMEPAD_BUTTON_EAST:
            return "<Gamepad>/buttonEast";
        case SDL_GAMEPAD_BUTTON_WEST:
            return "<Gamepad>/buttonWest";
        case SDL_GAMEPAD_BUTTON_NORTH:
            return "<Gamepad>/buttonNorth";
        case SDL_GAMEPAD_BUTTON_START:
            return "<Gamepad>/start";
        case SDL_GAMEPAD_BUTTON_BACK:
            return "<Gamepad>/select";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return "<Gamepad>/leftShoulder";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return "<Gamepad>/rightShoulder";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return "<Gamepad>/leftStickPress";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return "<Gamepad>/rightStickPress";
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return "<Gamepad>/dpad/up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return "<Gamepad>/dpad/down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return "<Gamepad>/dpad/left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return "<Gamepad>/dpad/right";
        default:
            return {};
        }
    }

    std::string_view GamepadAxisInputPath(const std::int32_t axisValue) noexcept
    {
        switch (static_cast<SDL_GamepadAxis>(axisValue))
        {
        case SDL_GAMEPAD_AXIS_LEFTX:
        case SDL_GAMEPAD_AXIS_LEFTY:
            return "<Gamepad>/leftStick";
        case SDL_GAMEPAD_AXIS_RIGHTX:
        case SDL_GAMEPAD_AXIS_RIGHTY:
            return "<Gamepad>/rightStick";
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return "<Gamepad>/leftTrigger";
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return "<Gamepad>/rightTrigger";
        default:
            return {};
        }
    }

    InputValue GamepadAxisInputValue(const std::int32_t axisValue, const std::int16_t rawValue,
                                     InputValue current) noexcept
    {
        switch (static_cast<SDL_GamepadAxis>(axisValue))
        {
        case SDL_GAMEPAD_AXIS_LEFTX:
        case SDL_GAMEPAD_AXIS_RIGHTX:
            current.Type = InputValueType::Axis2D;
            current.X = NormalizeInputAxis(rawValue);
            return current;
        case SDL_GAMEPAD_AXIS_LEFTY:
        case SDL_GAMEPAD_AXIS_RIGHTY:
            current.Type = InputValueType::Axis2D;
            current.Y = -NormalizeInputAxis(rawValue);
            return current;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return AxisInputValue(NormalizeInputTrigger(rawValue));
        default:
            return {};
        }
    }

    double InputParameterValue(const std::vector<InputParameter>& parameters, const std::string_view name,
                               const double fallback) noexcept
    {
        const auto found = std::ranges::find(parameters, name, &InputParameter::Name);
        return found == parameters.end() ? fallback : found->Value;
    }

    InputValue ApplyInputProcessors(InputValue value, const std::vector<InputBehaviorDefinition>& processors)
    {
        for (const auto& processor : processors)
        {
            if (processor.Name == "Deadzone")
            {
                const auto minimum = static_cast<float>(InputParameterValue(processor.Parameters, "minimum", 0.125));
                const auto maximum = static_cast<float>(InputParameterValue(processor.Parameters, "maximum", 0.925));
                const auto magnitude = value.Magnitude();
                if (magnitude <= minimum)
                {
                    value.X = 0.0F;
                    value.Y = 0.0F;
                }
                else if (magnitude > 0.0F)
                {
                    const auto scaled =
                        std::clamp((magnitude - minimum) / std::max(maximum - minimum, 0.001F), 0.0F, 1.0F);
                    value.X = value.X / magnitude * scaled;
                    value.Y = value.Y / magnitude * scaled;
                }
            }
            else if (processor.Name == "Scale")
            {
                value.X *= static_cast<float>(InputParameterValue(processor.Parameters, "x", 1.0));
                value.Y *= static_cast<float>(InputParameterValue(processor.Parameters, "y", 1.0));
            }
            else if (processor.Name == "Invert")
            {
                if (InputParameterValue(processor.Parameters, "x", 1.0) != 0.0)
                    value.X = -value.X;
                if (value.Type == InputValueType::Axis2D && InputParameterValue(processor.Parameters, "y", 1.0) != 0.0)
                    value.Y = -value.Y;
            }
            else if (processor.Name == "Normalize")
            {
                const auto magnitude = value.Magnitude();
                if (magnitude > 1.0F)
                {
                    value.X /= magnitude;
                    value.Y /= magnitude;
                }
            }
        }
        return value;
    }
} // namespace Keire::Detail
