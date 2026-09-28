#include "KeireClient/Editor/ViewportInputRouting.h"

#include <doctest/doctest.h>

#include <optional>

TEST_CASE("game viewport focus loss suppresses input without invalidating managed actions")
{
    const Keire::ManagedInputActionSnapshot original{
        Keire::InputActionPhase::Performed, {Keire::InputValueType::Axis2D, 1.0F, -0.5F}, 42, true, true, true, true};
    const auto inactive = KeireEditor::RouteGameViewportAction(original, false);
    REQUIRE(inactive);
    CHECK(inactive->Value.Type == Keire::InputValueType::Axis2D);
    CHECK(inactive->Value.X == 0.0F);
    CHECK(inactive->Value.Y == 0.0F);
    CHECK(inactive->Frame == 42);
    CHECK(inactive->Enabled);
    CHECK(inactive->Phase == Keire::InputActionPhase::Waiting);
    CHECK_FALSE(inactive->Started);
    CHECK_FALSE(inactive->Performed);
    CHECK_FALSE(inactive->Canceled);
    const auto active = KeireEditor::RouteGameViewportAction(original, true);
    REQUIRE(active);
    CHECK(active->Value.X == 1.0F);
    CHECK(active->Value.Y == -0.5F);
    CHECK(active->Performed);
    CHECK_FALSE(KeireEditor::RouteGameViewportAction(std::nullopt, false));
    CHECK_FALSE(KeireEditor::RouteGameViewportAction(std::nullopt, true));
    const auto disabled = KeireEditor::RouteGameViewportAction(Keire::ManagedInputActionSnapshot{}, false);
    REQUIRE(disabled);
    CHECK_FALSE(disabled->Enabled);
    CHECK(disabled->Phase == Keire::InputActionPhase::Disabled);
}

TEST_CASE("game viewport review releases capture and restores ownership when cancelled")
{
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(true, true, true, true, false, true));
    CHECK(KeireEditor::GameViewportOwnsRuntimeInput(true, true, true, true, false, false));
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(false, true, true, true, false, false));
}
