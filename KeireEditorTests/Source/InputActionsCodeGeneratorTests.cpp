#include "KeireClient/Editor/InputActionsCodeGenerator.h"

#include <doctest/doctest.h>

#include <string>

TEST_CASE("input action C# generation preserves stable IDs and sanitizes member names")
{
    auto definition = Keire::InputActionAsset::DefaultDefinition();
    definition.ActionMaps.front().Name = "Player Controls";
    definition.ActionMaps.front().Actions.front().Name = "Move & Aim";
    const auto source = KeireEditor::GenerateInputActionsCSharp(definition, "DefaultInputActions", "Example.Game");
    CHECK(source.find("namespace Example.Game;") != std::string::npos);
    CHECK(source.find("class DefaultInputActions") != std::string::npos);
    CHECK(source.find("PlayerControlsActions") != std::string::npos);
    CHECK(source.find("InputAction MoveAim") != std::string::npos);
    CHECK(source.find(std::to_string(definition.ActionMaps.front().Id.High()) + "UL") != std::string::npos);
    CHECK(source.find(std::to_string(definition.ActionMaps.front().Actions.front().Id.Low()) + "UL") !=
          std::string::npos);
}

TEST_CASE("input action C# generation rejects unsafe destinations")
{
    const auto definition = Keire::InputActionAsset::DefaultDefinition();
    CHECK_THROWS_AS((void)KeireEditor::GenerateInputActionsCSharp(definition, "../Input", "Example"),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)KeireEditor::GenerateInputActionsCSharp(definition, "InputActions", "Example..Game"),
                    std::invalid_argument);
}
