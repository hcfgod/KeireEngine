#include "KeireInternal/InputInternal.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
    struct InputPathCase final
    {
        std::int32_t Value = 0;
        std::string_view Path;
    };
} // namespace

TEST_CASE("Input internal helpers map keyboard controls and reject unknown scancodes")
{
    static constexpr InputPathCase cases[] = {
        {SDL_SCANCODE_A, "<Keyboard>/a"},
        {SDL_SCANCODE_Z, "<Keyboard>/z"},
        {SDL_SCANCODE_1, "<Keyboard>/digit1"},
        {SDL_SCANCODE_9, "<Keyboard>/digit9"},
        {SDL_SCANCODE_0, "<Keyboard>/digit0"},
        {SDL_SCANCODE_F1, "<Keyboard>/f1"},
        {SDL_SCANCODE_F12, "<Keyboard>/f12"},
        {SDL_SCANCODE_F13, "<Keyboard>/f13"},
        {SDL_SCANCODE_F24, "<Keyboard>/f24"},
        {SDL_SCANCODE_SPACE, "<Keyboard>/space"},
        {SDL_SCANCODE_RETURN, "<Keyboard>/enter"},
        {SDL_SCANCODE_KP_ENTER, "<Keyboard>/numpadEnter"},
        {SDL_SCANCODE_ESCAPE, "<Keyboard>/escape"},
        {SDL_SCANCODE_UP, "<Keyboard>/upArrow"},
        {SDL_SCANCODE_DOWN, "<Keyboard>/downArrow"},
        {SDL_SCANCODE_LEFT, "<Keyboard>/leftArrow"},
        {SDL_SCANCODE_RIGHT, "<Keyboard>/rightArrow"},
        {SDL_SCANCODE_TAB, "<Keyboard>/tab"},
        {SDL_SCANCODE_BACKSPACE, "<Keyboard>/backspace"},
        {SDL_SCANCODE_CAPSLOCK, "<Keyboard>/capsLock"},
        {SDL_SCANCODE_LSHIFT, "<Keyboard>/leftShift"},
        {SDL_SCANCODE_RSHIFT, "<Keyboard>/rightShift"},
        {SDL_SCANCODE_LCTRL, "<Keyboard>/leftCtrl"},
        {SDL_SCANCODE_RCTRL, "<Keyboard>/rightCtrl"},
        {SDL_SCANCODE_LALT, "<Keyboard>/leftAlt"},
        {SDL_SCANCODE_RALT, "<Keyboard>/rightAlt"},
        {SDL_SCANCODE_LGUI, "<Keyboard>/leftMeta"},
        {SDL_SCANCODE_RGUI, "<Keyboard>/rightMeta"},
        {SDL_SCANCODE_INSERT, "<Keyboard>/insert"},
        {SDL_SCANCODE_DELETE, "<Keyboard>/delete"},
        {SDL_SCANCODE_HOME, "<Keyboard>/home"},
        {SDL_SCANCODE_END, "<Keyboard>/end"},
        {SDL_SCANCODE_PAGEUP, "<Keyboard>/pageUp"},
        {SDL_SCANCODE_PAGEDOWN, "<Keyboard>/pageDown"},
        {SDL_SCANCODE_PRINTSCREEN, "<Keyboard>/printScreen"},
        {SDL_SCANCODE_SCROLLLOCK, "<Keyboard>/scrollLock"},
        {SDL_SCANCODE_PAUSE, "<Keyboard>/pause"},
        {SDL_SCANCODE_MINUS, "<Keyboard>/minus"},
        {SDL_SCANCODE_EQUALS, "<Keyboard>/equals"},
        {SDL_SCANCODE_LEFTBRACKET, "<Keyboard>/leftBracket"},
        {SDL_SCANCODE_RIGHTBRACKET, "<Keyboard>/rightBracket"},
        {SDL_SCANCODE_BACKSLASH, "<Keyboard>/backslash"},
        {SDL_SCANCODE_SEMICOLON, "<Keyboard>/semicolon"},
        {SDL_SCANCODE_APOSTROPHE, "<Keyboard>/quote"},
        {SDL_SCANCODE_GRAVE, "<Keyboard>/backquote"},
        {SDL_SCANCODE_COMMA, "<Keyboard>/comma"},
        {SDL_SCANCODE_PERIOD, "<Keyboard>/period"},
        {SDL_SCANCODE_SLASH, "<Keyboard>/slash"},
        {SDL_SCANCODE_NUMLOCKCLEAR, "<Keyboard>/numLock"},
        {SDL_SCANCODE_KP_0, "<Keyboard>/numpad0"},
        {SDL_SCANCODE_KP_1, "<Keyboard>/numpad1"},
        {SDL_SCANCODE_KP_9, "<Keyboard>/numpad9"},
        {SDL_SCANCODE_KP_PLUS, "<Keyboard>/numpadPlus"},
        {SDL_SCANCODE_KP_MINUS, "<Keyboard>/numpadMinus"},
        {SDL_SCANCODE_KP_MULTIPLY, "<Keyboard>/numpadMultiply"},
        {SDL_SCANCODE_KP_DIVIDE, "<Keyboard>/numpadDivide"},
        {SDL_SCANCODE_KP_DECIMAL, "<Keyboard>/numpadPeriod"},
    };

    for (const auto& inputCase : cases)
    {
        CAPTURE(inputCase.Value);
        CHECK(Keire::Detail::KeyboardInputPath(inputCase.Value) == inputCase.Path);
    }
    CHECK(Keire::Detail::KeyboardInputPath(SDL_SCANCODE_UNKNOWN).empty());
    CHECK(Keire::Detail::KeyboardInputPath(std::numeric_limits<std::int32_t>::max()).empty());
}

TEST_CASE("Input internal helpers map gamepad controls and reject unknown enums")
{
    static constexpr InputPathCase buttonCases[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, "<Gamepad>/buttonSouth"},
        {SDL_GAMEPAD_BUTTON_EAST, "<Gamepad>/buttonEast"},
        {SDL_GAMEPAD_BUTTON_WEST, "<Gamepad>/buttonWest"},
        {SDL_GAMEPAD_BUTTON_NORTH, "<Gamepad>/buttonNorth"},
        {SDL_GAMEPAD_BUTTON_START, "<Gamepad>/start"},
        {SDL_GAMEPAD_BUTTON_BACK, "<Gamepad>/select"},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "<Gamepad>/leftShoulder"},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "<Gamepad>/rightShoulder"},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, "<Gamepad>/leftStickPress"},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "<Gamepad>/rightStickPress"},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, "<Gamepad>/dpad/up"},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "<Gamepad>/dpad/down"},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "<Gamepad>/dpad/left"},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "<Gamepad>/dpad/right"},
    };
    static constexpr InputPathCase axisCases[] = {
        {SDL_GAMEPAD_AXIS_LEFTX, "<Gamepad>/leftStick"},
        {SDL_GAMEPAD_AXIS_LEFTY, "<Gamepad>/leftStick"},
        {SDL_GAMEPAD_AXIS_RIGHTX, "<Gamepad>/rightStick"},
        {SDL_GAMEPAD_AXIS_RIGHTY, "<Gamepad>/rightStick"},
        {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, "<Gamepad>/leftTrigger"},
        {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, "<Gamepad>/rightTrigger"},
    };

    for (const auto& inputCase : buttonCases)
    {
        CAPTURE(inputCase.Value);
        CHECK(Keire::Detail::GamepadButtonInputPath(inputCase.Value) == inputCase.Path);
    }
    for (const auto& inputCase : axisCases)
    {
        CAPTURE(inputCase.Value);
        CHECK(Keire::Detail::GamepadAxisInputPath(inputCase.Value) == inputCase.Path);
    }
    CHECK(Keire::Detail::GamepadButtonInputPath(-1).empty());
    CHECK(Keire::Detail::GamepadAxisInputPath(-1).empty());
}

TEST_CASE("Input internal gamepad axis conversion preserves stick components and normalizes triggers")
{
    const auto horizontal = Keire::Detail::GamepadAxisInputValue(SDL_GAMEPAD_AXIS_LEFTX, 16384,
                                                                 Keire::Detail::VectorInputValue(-0.25F, 0.75F));
    CHECK(horizontal.Type == Keire::InputValueType::Axis2D);
    CHECK(horizontal.X == doctest::Approx(16384.0F / 32767.0F));
    CHECK(horizontal.Y == doctest::Approx(0.75F));

    const auto vertical = Keire::Detail::GamepadAxisInputValue(SDL_GAMEPAD_AXIS_RIGHTY, 8192,
                                                               Keire::Detail::VectorInputValue(-0.25F, 0.75F));
    CHECK(vertical.Type == Keire::InputValueType::Axis2D);
    CHECK(vertical.X == doctest::Approx(-0.25F));
    CHECK(vertical.Y == doctest::Approx(-8192.0F / 32767.0F));

    const auto minimumTrigger = Keire::Detail::GamepadAxisInputValue(SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
                                                                     std::numeric_limits<std::int16_t>::min(), {});
    const auto centeredTrigger = Keire::Detail::GamepadAxisInputValue(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0, {.Y = 1.0F});
    const auto maximumTrigger = Keire::Detail::GamepadAxisInputValue(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                                                                     std::numeric_limits<std::int16_t>::max(), {});
    CHECK(minimumTrigger.Type == Keire::InputValueType::Axis1D);
    CHECK(minimumTrigger.X == doctest::Approx(0.0F));
    CHECK(minimumTrigger.Y == doctest::Approx(0.0F));
    CHECK(centeredTrigger.Type == Keire::InputValueType::Axis1D);
    CHECK(centeredTrigger.X == doctest::Approx(0.5F));
    CHECK(centeredTrigger.Y == doctest::Approx(0.0F));
    CHECK(maximumTrigger.Type == Keire::InputValueType::Axis1D);
    CHECK(maximumTrigger.X == doctest::Approx(1.0F));

    const auto unknown = Keire::Detail::GamepadAxisInputValue(-1, 1234, Keire::Detail::VectorInputValue(0.5F, 0.5F));
    CHECK(unknown.Type == Keire::InputValueType::Boolean);
    CHECK(unknown.X == doctest::Approx(0.0F));
    CHECK(unknown.Y == doctest::Approx(0.0F));
}

TEST_CASE("Input internal processors apply documented defaults and ordered transformations")
{
    SUBCASE("Deadzone clears small values and rescales larger vectors")
    {
        const auto cleared =
            Keire::Detail::ApplyInputProcessors(Keire::Detail::VectorInputValue(0.1F, 0.0F), {{"Deadzone", {}}});
        CHECK(cleared.Type == Keire::InputValueType::Axis2D);
        CHECK(cleared.X == doctest::Approx(0.0F));
        CHECK(cleared.Y == doctest::Approx(0.0F));

        const auto scaled = Keire::Detail::ApplyInputProcessors(Keire::Detail::VectorInputValue(0.3F, 0.4F),
                                                                {{"Deadzone", {{"minimum", 0.2}, {"maximum", 1.0}}}});
        CHECK(scaled.X == doctest::Approx(0.225F));
        CHECK(scaled.Y == doctest::Approx(0.3F));
    }

    SUBCASE("Scale falls back independently and processors retain their declared order")
    {
        const auto scaled = Keire::Detail::ApplyInputProcessors(Keire::Detail::VectorInputValue(2.0F, -3.0F),
                                                                {{"Scale", {{"x", 0.5}}}});
        CHECK(scaled.X == doctest::Approx(1.0F));
        CHECK(scaled.Y == doctest::Approx(-3.0F));

        const auto ordered = Keire::Detail::ApplyInputProcessors(
            Keire::Detail::VectorInputValue(0.25F, -0.5F),
            {{"Scale", {{"x", 2.0}, {"y", 3.0}}}, {"Invert", {{"x", 1.0}, {"y", 0.0}}}});
        CHECK(ordered.X == doctest::Approx(-0.5F));
        CHECK(ordered.Y == doctest::Approx(-1.5F));
    }

    SUBCASE("Invert respects disabled axes and one-dimensional values")
    {
        const auto vector = Keire::Detail::ApplyInputProcessors(Keire::Detail::VectorInputValue(0.25F, -0.75F),
                                                                {{"Invert", {{"x", 0.0}}}});
        CHECK(vector.X == doctest::Approx(0.25F));
        CHECK(vector.Y == doctest::Approx(0.75F));

        const auto axis =
            Keire::Detail::ApplyInputProcessors({Keire::InputValueType::Axis1D, 0.25F, 0.5F}, {{"Invert", {}}});
        CHECK(axis.X == doctest::Approx(-0.25F));
        CHECK(axis.Y == doctest::Approx(0.5F));
    }

    SUBCASE("Normalize clamps only magnitudes above one and unknown processors are inert")
    {
        const auto normalized =
            Keire::Detail::ApplyInputProcessors(Keire::Detail::VectorInputValue(3.0F, 4.0F), {{"Normalize", {}}});
        CHECK(normalized.X == doctest::Approx(0.6F));
        CHECK(normalized.Y == doctest::Approx(0.8F));

        const auto unchanged = Keire::Detail::ApplyInputProcessors(Keire::Detail::VectorInputValue(0.3F, 0.4F),
                                                                   {{"Normalize", {}}, {"Unknown", {{"x", 99.0}}}});
        CHECK(unchanged.X == doctest::Approx(0.3F));
        CHECK(unchanged.Y == doctest::Approx(0.4F));
    }
}

TEST_CASE("Input internal parameter lookup returns exact matches and caller fallbacks")
{
    const std::vector<Keire::InputParameter> parameters = {{"minimum", 0.25}, {"minimum", 0.75}, {"Minimum", 0.5}};
    CHECK(Keire::Detail::InputParameterValue(parameters, "minimum", 0.125) == doctest::Approx(0.25));
    CHECK(Keire::Detail::InputParameterValue(parameters, "Minimum", 0.125) == doctest::Approx(0.5));
    CHECK(Keire::Detail::InputParameterValue(parameters, "maximum", 0.925) == doctest::Approx(0.925));
    CHECK(Keire::Detail::InputParameterValue({}, "x", -2.0) == doctest::Approx(-2.0));
}
