#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include "KeireInternal/Ui/RuntimeUiDrawCommandsInternal.h"

#include <vector>

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

TEST_CASE("Runtime UI text respects scaled padding and preserves the element transform pivot")
{
    Keire::RuntimeUiElementState state;
    state.Rect = {10.0F, 20.0F, 200.0F, 100.0F};
    state.ClipRect = state.Rect;
    state.Content.Text = "Padded button";
    state.Style.Padding = {12.0F, 4.0F, 8.0F, 6.0F};
    state.Style.TransformOrigin = {0.25F, 0.75F};
    state.Style.RotationDegrees = 15.0F;
    for (const float scale : {1.0F, 1.5F, 2.0F})
    {
        std::vector<Keire::RuntimeUiDrawCommand> draws;
        Keire::Detail::AppendRuntimeUiDrawCommands(draws, state, {}, scale);
        std::size_t textCount = 0;
        for (const auto& draw : draws)
        {
            if (draw.Type != Keire::RuntimeUiDrawType::Text)
                continue;
            ++textCount;
            CHECK(draw.Rect.X == doctest::Approx(10.0F + 12.0F * scale));
            CHECK(draw.Rect.Y == doctest::Approx(20.0F + 4.0F * scale));
            CHECK(draw.Rect.Width == doctest::Approx(200.0F - 20.0F * scale));
            CHECK(draw.Rect.Height == doctest::Approx(100.0F - 10.0F * scale));
            CHECK(draw.Rect.X + draw.Rect.Width * draw.TransformOrigin.X == doctest::Approx(60.0F));
            CHECK(draw.Rect.Y + draw.Rect.Height * draw.TransformOrigin.Y == doctest::Approx(95.0F));
        }
        CHECK(textCount == 1);
    }
    state.Style.Padding = {110.0F, 0.0F, 110.0F, 0.0F};
    std::vector<Keire::RuntimeUiDrawCommand> draws;
    Keire::Detail::AppendRuntimeUiDrawCommands(draws, state, {}, 1.0F);
    for (const auto& draw : draws)
        CHECK(draw.Type != Keire::RuntimeUiDrawType::Text);
    state.Style.Padding = {};
    draws.clear();
    Keire::Detail::AppendRuntimeUiDrawCommands(draws, state, {}, 1.0F);
    bool found = false;
    for (const auto& draw : draws)
    {
        if (draw.Type != Keire::RuntimeUiDrawType::Text)
            continue;
        found = true;
        CHECK(draw.Rect.X == state.Rect.X);
        CHECK(draw.Rect.Width == state.Rect.Width);
        CHECK(draw.TransformOrigin.X == doctest::Approx(state.Style.TransformOrigin.X));
    }
    CHECK(found);
}

TEST_CASE("Runtime UI progress bars fill without slider insets or handles")
{
    Keire::RuntimeUiElementState state;
    state.Type = Keire::RuntimeUiElementType::ProgressBar;
    state.Rect = {10.0F, 20.0F, 100.0F, 8.0F};
    state.ClipRect = state.Rect;
    state.Control.Minimum = 0.0F;
    state.Control.Maximum = 100.0F;
    state.Control.Value = 40.0F;
    state.Style.Foreground = {0.2F, 0.8F, 1.0F, 1.0F};
    state.Style.CornerRadius = 4.0F;

    std::vector<Keire::RuntimeUiDrawCommand> draws;
    Keire::Detail::AppendRuntimeUiDrawCommands(draws, state, {}, 1.0F);

    REQUIRE(draws.size() == 1U);
    CHECK(draws.front().Rect.X == doctest::Approx(10.0F));
    CHECK(draws.front().Rect.Y == doctest::Approx(20.0F));
    CHECK(draws.front().Rect.Width == doctest::Approx(40.0F));
    CHECK(draws.front().Rect.Height == doctest::Approx(8.0F));
    CHECK(draws.front().CornerRadius == doctest::Approx(4.0F));
}

TEST_CASE("UI Toolkit intrinsic text size includes authored padding")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto root = tree->Create(Keire::RuntimeUiElementType::HorizontalLayout);
    const auto button = tree->Create(Keire::RuntimeUiElementType::Button, root);
    Keire::RuntimeUiStyle style;
    style.FontSize = 20.0F;
    style.Padding = {12.0F, 4.0F, 8.0F, 6.0F};
    REQUIRE(tree->SetStyle(button, style));
    REQUIRE(tree->SetContent(button, {.Text = "Hello"}));
    tree->Layout(300.0F, 100.0F, {}, {.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels});
    const auto state = tree->State(button);
    REQUIRE(state);
    CHECK(state->Rect.Width >= 91.0F);
    CHECK(state->Rect.Height >= 45.0F);
}
