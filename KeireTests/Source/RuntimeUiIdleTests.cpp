#include "Keire/Core.h"
#include <doctest/doctest.h>
#include <limits>
#include <stdexcept>

TEST_CASE("Runtime UI idle caches survive destruction reuse and clearing")
{
    const auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto root = tree->Create(Keire::RuntimeUiElementType::Panel);
    const auto child = tree->Create(Keire::RuntimeUiElementType::Panel, root);
    CHECK(tree->Statistics().Elements == 2);
    Keire::RuntimeUiStyle style;
    style.Width = 20.0F;
    style.Height = 20.0F;
    style.Background = {1.0F, 0.0F, 0.0F, 1.0F};
    REQUIRE(tree->SetStyle(child, style));
    tree->Layout(100.0F, 100.0F);
    CHECK(tree->Statistics().DrawBatches > 0);
    REQUIRE(tree->Destroy(root));
    CHECK(tree->Statistics().Elements == 0);
    CHECK_FALSE(tree->Destroy(child));
    CHECK_THROWS_AS((void)tree->Create(Keire::RuntimeUiElementType::Panel, child), std::invalid_argument);
    CHECK(tree->Statistics().Elements == 0);
    tree->Layout(100.0F, 100.0F);
    CHECK(tree->Statistics().DrawBatches == 0);
    const auto replacement = tree->Create(Keire::RuntimeUiElementType::Panel);
    CHECK(tree->Statistics().Elements == 1);
    REQUIRE(tree->SetStyle(replacement, style));
    tree->Layout(100.0F, 100.0F);
    CHECK(tree->Statistics().DrawBatches > 0);
    tree->Clear();
    CHECK(tree->Statistics().Elements == 0);
    CHECK(tree->Statistics().DrawBatches == 0);
    CHECK(tree->DrawCommands().empty());
}

TEST_CASE("Runtime UI idle transition shortcut preserves validation and restart lifecycle")
{
    const auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    CHECK_FALSE(tree->AdvanceTransitions(1.0F));
    CHECK_THROWS_AS((void)tree->AdvanceTransitions(-1.0F), std::invalid_argument);
    CHECK_THROWS_AS((void)tree->AdvanceTransitions(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        const auto element = tree->Create(Keire::RuntimeUiElementType::Panel);
        Keire::RuntimeUiStyle style;
        style.Width = 10.0F;
        style.TransitionProperties[0] = Keire::RuntimeUiTransitionProperty::Width;
        style.TransitionPropertyCount = 1;
        style.TransitionDurations[0] = 1.0F;
        style.TransitionDurationCount = 1;
        REQUIRE(tree->SetStyle(element, style));
        style.Width = 30.0F;
        REQUIRE(tree->SetStyle(element, style));
        REQUIRE(tree->AdvanceTransitions(0.5F));
        CHECK(tree->State(element)->Style.Width == doctest::Approx(20.0F));
        style.Width = 40.0F;
        REQUIRE(tree->SetStyle(element, style));
        REQUIRE(tree->AdvanceTransitions(1.0F));
        CHECK(tree->State(element)->Style.Width == doctest::Approx(40.0F));
        CHECK_FALSE(tree->AdvanceTransitions(0.1F));
        style.Width = 50.0F;
        REQUIRE(tree->SetStyle(element, style));
        if (cycle == 0)
        {
            REQUIRE(tree->Destroy(element));
            CHECK_FALSE(tree->AdvanceTransitions(0.1F));
        }
        else
        {
            tree->Clear();
            CHECK_FALSE(tree->AdvanceTransitions(0.1F));
        }
    }
}
