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
    CHECK(source.find("using System;") != std::string::npos);
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
    CHECK_THROWS_AS((void)KeireEditor::GenerateInputActionsCSharp(definition, "InputActions", "Example.Game."),
                    std::invalid_argument);
}

TEST_CASE("input action C# generation avoids wrapper member collisions and escapes control characters")
{
    auto definition = Keire::InputActionAsset::DefaultDefinition();
    definition.ActionMaps[0].Name = "Enable";
    definition.ActionMaps[0].Actions[0].Name = "Enable";
    definition.ActionMaps[1].Name = "EnableActions";
    definition.ActionMaps[1].Actions[0].Name = "EnableActionsActions";
    definition.ActionMaps[1].Actions[1].Name = "Line\n\"Break";

    const auto source = KeireEditor::GenerateInputActionsCSharp(definition, "DefaultInputActions", "Example.Game");
    CHECK(source.find("public Enable2Actions Enable2 { get; }") != std::string::npos);
    CHECK(source.find("public InputAction Enable2 =>") != std::string::npos);
    CHECK(source.find("public InputAction EnableActionsActions2 =>") != std::string::npos);
    CHECK(source.find("Line\\u000a\\\"Break") != std::string::npos);
}
