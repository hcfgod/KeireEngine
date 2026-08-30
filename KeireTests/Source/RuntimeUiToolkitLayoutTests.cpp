#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

TEST_CASE("UI Toolkit resolves percentage positioning and dimensions against the containing block")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto root = tree->Create(Keire::RuntimeUiElementType::Panel);
    const auto child = tree->Create(Keire::RuntimeUiElementType::Panel, root);
    Keire::RuntimeUiStyle childStyle;
    childStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    childStyle.XPercent = 0.25F;
    childStyle.YPercent = 0.10F;
    childStyle.WidthPercent = 0.50F;
    childStyle.HeightPercent = 0.25F;
    REQUIRE(tree->SetStyle(child, childStyle));

    tree->Layout(200.0F, 100.0F, {}, {.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels});
    const auto state = tree->State(child);
    REQUIRE(state);
    CHECK(state->Rect.X == doctest::Approx(50.0F));
    CHECK(state->Rect.Y == doctest::Approx(10.0F));
    CHECK(state->Rect.Width == doctest::Approx(100.0F));
    CHECK(state->Rect.Height == doctest::Approx(25.0F));
}

TEST_CASE("UI Toolkit flex rows shrink and justify their children deterministically")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto root = tree->Create(Keire::RuntimeUiElementType::HorizontalLayout);
    const auto first = tree->Create(Keire::RuntimeUiElementType::Panel, root);
    const auto second = tree->Create(Keire::RuntimeUiElementType::Panel, root);
    Keire::RuntimeUiStyle childStyle;
    childStyle.Width = 80.0F;
    childStyle.Height = 20.0F;
    REQUIRE(tree->SetStyle(first, childStyle));
    REQUIRE(tree->SetStyle(second, childStyle));

    tree->Layout(100.0F, 40.0F, {}, {.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels});
    REQUIRE(tree->State(first));
    REQUIRE(tree->State(second));
    CHECK(tree->State(first)->Rect.Width == doctest::Approx(50.0F));
    CHECK(tree->State(second)->Rect.Width == doctest::Approx(50.0F));

    childStyle.Width = 20.0F;
    childStyle.FlexShrink = 0.0F;
    REQUIRE(tree->SetStyle(first, childStyle));
    REQUIRE(tree->SetStyle(second, childStyle));
    Keire::RuntimeUiStyle rootStyle;
    rootStyle.JustifyContent = Keire::RuntimeUiJustification::SpaceBetween;
    rootStyle.ForceExpandWidth = false;
    REQUIRE(tree->SetStyle(root, rootStyle));
    tree->Layout(100.0F, 40.0F, {}, {.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels});
    CHECK(tree->State(first)->Rect.X == doctest::Approx(0.0F));
    CHECK(tree->State(second)->Rect.X == doctest::Approx(80.0F));
}

TEST_CASE("UI Toolkit flex containers derive auto main-axis size from their content")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto root = tree->Create(Keire::RuntimeUiElementType::VerticalLayout);
    const auto card = tree->Create(Keire::RuntimeUiElementType::VerticalLayout, root);
    const auto title = tree->Create(Keire::RuntimeUiElementType::Text, card);
    const auto button = tree->Create(Keire::RuntimeUiElementType::Button, card);

    Keire::RuntimeUiStyle rootStyle;
    rootStyle.ForceExpandHeight = false;
    REQUIRE(tree->SetStyle(root, rootStyle));
    Keire::RuntimeUiStyle cardStyle;
    cardStyle.Width = 200.0F;
    cardStyle.Padding = {10.0F, 12.0F, 10.0F, 12.0F};
    cardStyle.Gap = 8.0F;
    cardStyle.ForceExpandHeight = false;
    REQUIRE(tree->SetStyle(card, cardStyle));
    Keire::RuntimeUiStyle titleStyle;
    titleStyle.Height = 30.0F;
    REQUIRE(tree->SetStyle(title, titleStyle));
    Keire::RuntimeUiStyle buttonStyle;
    buttonStyle.Height = 40.0F;
    REQUIRE(tree->SetStyle(button, buttonStyle));

    tree->Layout(320.0F, 240.0F, {}, {.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels});
    REQUIRE(tree->State(card));
    REQUIRE(tree->State(button));
    CHECK(tree->State(card)->Rect.Height == doctest::Approx(102.0F));
    CHECK(tree->State(button)->Rect.Height == doctest::Approx(40.0F));
    CHECK_FALSE(tree->State(button)->ClipRect.Empty());
}

TEST_CASE("UI Toolkit flex wrapping creates bounded deterministic lines")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto root = tree->Create(Keire::RuntimeUiElementType::HorizontalLayout);
    const auto first = tree->Create(Keire::RuntimeUiElementType::Panel, root);
    const auto second = tree->Create(Keire::RuntimeUiElementType::Panel, root);
    const auto third = tree->Create(Keire::RuntimeUiElementType::Panel, root);
    Keire::RuntimeUiStyle rootStyle;
    rootStyle.Wrap = Keire::RuntimeUiWrapMode::Wrap;
    rootStyle.Gap = 5.0F;
    REQUIRE(tree->SetStyle(root, rootStyle));
    Keire::RuntimeUiStyle childStyle;
    childStyle.Width = 60.0F;
    childStyle.Height = 20.0F;
    REQUIRE(tree->SetStyle(first, childStyle));
    REQUIRE(tree->SetStyle(second, childStyle));
    REQUIRE(tree->SetStyle(third, childStyle));

    tree->Layout(100.0F, 100.0F, {}, {.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels});
    REQUIRE(tree->State(first));
    REQUIRE(tree->State(second));
    REQUIRE(tree->State(third));
    CHECK(tree->State(first)->Rect.Y == doctest::Approx(0.0F));
    CHECK(tree->State(second)->Rect.Y == doctest::Approx(25.0F));
    CHECK(tree->State(third)->Rect.Y == doctest::Approx(50.0F));
    CHECK(tree->State(third)->Rect.Y + tree->State(third)->Rect.Height <= doctest::Approx(100.0F));
}

TEST_CASE("UI Toolkit shared roots keep independent panel scaling and safe-area layout")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto safeRoot = tree->Create(Keire::RuntimeUiElementType::Panel);
    const auto scaledRoot = tree->Create(Keire::RuntimeUiElementType::Panel);
    const auto legacyRoot = tree->Create(Keire::RuntimeUiElementType::Panel);
    const auto safeChild = tree->Create(Keire::RuntimeUiElementType::Panel, safeRoot);
    const auto scaledChild = tree->Create(Keire::RuntimeUiElementType::Panel, scaledRoot);
    Keire::RuntimeUiStyle childStyle;
    childStyle.Width = 20.0F;
    childStyle.Height = 10.0F;
    REQUIRE(tree->SetStyle(safeChild, childStyle));
    REQUIRE(tree->SetStyle(scaledChild, childStyle));

    Keire::RuntimeUiCanvasSettings safeSettings;
    safeSettings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    safeSettings.RespectSafeArea = true;
    REQUIRE(tree->SetRootCanvasSettings(safeRoot, safeSettings));
    Keire::RuntimeUiCanvasSettings scaledSettings;
    scaledSettings.ScaleMode = Keire::RuntimeUiScaleMode::ScaleWithViewport;
    scaledSettings.ReferenceWidth = 100.0F;
    scaledSettings.ReferenceHeight = 100.0F;
    scaledSettings.MatchWidthOrHeight = 0.0F;
    scaledSettings.RespectSafeArea = false;
    REQUIRE(tree->SetRootCanvasSettings(scaledRoot, scaledSettings));
    CHECK_FALSE(tree->SetRootCanvasSettings(scaledChild, scaledSettings));

    Keire::RuntimeUiCanvasSettings fallback;
    fallback.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    fallback.RespectSafeArea = false;
    tree->Layout(200.0F, 100.0F, {10.0F, 5.0F, 20.0F, 15.0F}, fallback);
    REQUIRE(tree->State(safeRoot));
    REQUIRE(tree->State(scaledRoot));
    REQUIRE(tree->State(legacyRoot));
    REQUIRE(tree->State(safeChild));
    REQUIRE(tree->State(scaledChild));
    CHECK(tree->State(safeRoot)->Rect == (Keire::RuntimeUiRect{10.0F, 5.0F, 170.0F, 80.0F}));
    CHECK(tree->State(scaledRoot)->Rect == (Keire::RuntimeUiRect{0.0F, 0.0F, 200.0F, 100.0F}));
    CHECK(tree->State(legacyRoot)->Rect == (Keire::RuntimeUiRect{0.0F, 0.0F, 200.0F, 100.0F}));
    CHECK(tree->State(safeChild)->Rect.Width == doctest::Approx(20.0F));
    CHECK(tree->State(scaledChild)->Rect.Width == doctest::Approx(40.0F));
    const auto first = tree->Statistics();

    REQUIRE(tree->SetRootCanvasSettings(scaledRoot, scaledSettings));
    tree->Layout(200.0F, 100.0F, {10.0F, 5.0F, 20.0F, 15.0F}, fallback);
    const auto reused = tree->Statistics();
    CHECK(reused.LayoutPasses == first.LayoutPasses);
    CHECK(reused.ReusedLayoutPasses == first.ReusedLayoutPasses + 1);

    scaledSettings.ReferenceWidth = 200.0F;
    REQUIRE(tree->SetRootCanvasSettings(scaledRoot, scaledSettings));
    tree->Layout(200.0F, 100.0F, {10.0F, 5.0F, 20.0F, 15.0F}, fallback);
    const auto changed = tree->Statistics();
    CHECK(changed.LayoutPasses == reused.LayoutPasses + 1);
    CHECK(tree->State(safeChild)->Rect.Width == doctest::Approx(20.0F));
    CHECK(tree->State(scaledChild)->Rect.Width == doctest::Approx(20.0F));
}
