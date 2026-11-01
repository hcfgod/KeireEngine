#include "KeireClient/Editor/ViewportInputRouting.h"

#include <doctest/doctest.h>

#include <optional>

TEST_CASE("editor text input isolates gameplay paste commit and focus restoration")
{
    KeireEditor::EditorTextInputGate gate;
    const Keire::ManagedInputActionSnapshot action{
        Keire::InputActionPhase::Performed, {Keire::InputValueType::Axis2D, 1.0F, -0.5F}, 42, true, true, true, true};
    const Keire::InputControlSnapshot key{{}, {Keire::InputValueType::Boolean, 1.0F, 0.0F}, 42, true, true};
    gate.Observe(40, false);
    CHECK(gate.AllowsGameplay());
    // Persistent Play ownership stays intact while an editor numeric/text field owns text input.
    CHECK(KeireEditor::GameViewportOwnsRuntimeInput(true, true, false, false, false));
    gate.Observe(41, true);
    gate.Observe(42, true);
    for (const bool commitFrame : {false, true})
    {
        if (commitFrame)
        {
            gate.Observe(43, false);
        }
        CHECK_FALSE(gate.AllowsGameplay());
        const auto routedAction = KeireEditor::RouteGameViewportAction(action, gate.AllowsGameplay());
        REQUIRE(routedAction);
        CHECK(routedAction->Enabled);
        CHECK(routedAction->Value.X == 0.0F);
        CHECK_FALSE(routedAction->Started);
        CHECK_FALSE(routedAction->Performed);
        CHECK_FALSE(routedAction->Canceled);
        const auto routedKey = KeireEditor::RouteGameViewportControl(key, gate.AllowsGameplay());
        REQUIRE(routedKey);
        CHECK_FALSE(routedKey->IsPressed());
        CHECK_FALSE(routedKey->Pressed);
        CHECK_FALSE(routedKey->Released);
    }
    // Multiple reads in the commit frame cannot consume the neutral-frame barrier.
    gate.Observe(43, false);
    CHECK_FALSE(gate.AllowsGameplay());
    gate.Observe(44, false);
    CHECK(gate.AllowsGameplay());
    auto heldKey = key;
    heldKey.Pressed = false;
    heldKey.Released = false;
    const auto restored = KeireEditor::RouteGameViewportControl(heldKey, gate.AllowsGameplay());
    REQUIRE(restored);
    CHECK(restored->IsPressed());
    CHECK_FALSE(restored->Pressed);
    CHECK_FALSE(restored->Released);
    CHECK(KeireEditor::RouteGameViewportAction(action, gate.AllowsGameplay())->Performed);
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(true, true, true, false, true));
    CHECK_FALSE(KeireEditor::RouteGameViewportControl(std::nullopt, false));
}

TEST_CASE("editor text gate leaves ordinary HUD focus alone and captures late same-frame editing")
{
    KeireEditor::EditorTextInputGate gate;
    gate.Observe(1, false);
    CHECK(gate.AllowsGameplay());
    gate.Observe(1, true);
    gate.Observe(1, false);
    CHECK_FALSE(gate.AllowsGameplay());
    gate.Observe(2, false);
    CHECK_FALSE(gate.AllowsGameplay());
    gate.Observe(3, false);
    CHECK(gate.AllowsGameplay());
}

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

TEST_CASE("play input stays with Game until released and Scene navigation requires an explicit click")
{
    KeireEditor::SceneViewportNavigationState scene;
    // Hovering or even clicking another panel cannot steal input from an unlocked-cursor game.
    CHECK(KeireEditor::GameViewportOwnsRuntimeInput(true, true, false, false, false));
    CHECK_FALSE(scene.Update(true, false, true, true));
    // Escape releases gameplay, but hovering Scene or its stale keyboard focus is not consent to fly.
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(true, true, true, false, true));
    CHECK_FALSE(scene.Update(true, true, true, false));
    CHECK(scene.Update(true, true, true, true));
    CHECK(scene.Update(true, true, true, false));
    // Clicking Game, changing focus, or suspending the application revokes Scene navigation.
    CHECK_FALSE(scene.Update(true, false, false, false));
    CHECK_FALSE(scene.Update(true, true, true, false));
    CHECK(scene.Update(true, true, true, true));
    CHECK_FALSE(scene.Update(true, true, false, false));
    CHECK_FALSE(scene.Update(true, true, true, false));
    CHECK_FALSE(KeireEditor::GameViewportOwnsRuntimeInput(true, false, true, true, false));
    CHECK_FALSE(scene.Update(true, false, true, true));
    // Edit mode retains the existing hover navigation; a new Play session starts unclaimed by Scene.
    CHECK(scene.Update(false, true, false, false));
    CHECK_FALSE(scene.Update(true, true, true, false));
    CHECK(scene.Update(true, true, true, true));
    scene.Reset();
    CHECK_FALSE(scene.Update(true, true, true, false));
}
TEST_CASE("Scene navigation accepts the canvas click that establishes dock focus")
{
    KeireEditor::SceneViewportNavigationState scene;
    CHECK_FALSE(scene.Update(true, false, false, true));
    CHECK_FALSE(scene.Update(true, true, false, false));
    CHECK(scene.Update(true, true, false, true));
    CHECK(scene.Update(true, true, true, false));
    CHECK_FALSE(scene.Update(true, true, false, false));
    CHECK_FALSE(scene.Update(true, true, true, false));
    CHECK(scene.Update(true, true, true, true));
    CHECK_FALSE(scene.Update(true, false, true, true));
}
