#include "Keire/Core.h"

#include <doctest/doctest.h>

#include <cstddef>

TEST_CASE("retained runtime UI lays out, draws, hit-tests, and emits clicks")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto canvas = tree->Create(Keire::RuntimeUiElementType::Canvas);
    const auto button = tree->Create(Keire::RuntimeUiElementType::Button, canvas);

    Keire::RuntimeUiStyle canvasStyle;
    canvasStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    canvasStyle.Width = 800.0F;
    canvasStyle.Height = 600.0F;
    REQUIRE(tree->SetStyle(canvas, canvasStyle));

    Keire::RuntimeUiStyle buttonStyle;
    buttonStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    buttonStyle.X = 20.0F;
    buttonStyle.Y = 30.0F;
    buttonStyle.Width = 180.0F;
    buttonStyle.Height = 48.0F;
    buttonStyle.Background = {0.10F, 0.45F, 0.95F, 1.0F};
    REQUIRE(tree->SetStyle(button, buttonStyle));
    REQUIRE(tree->SetInteractable(button, true));
    REQUIRE(tree->SetContent(button, {.Text = "Launch"}));

    Keire::RuntimeUiCanvasSettings settings;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    tree->Layout(800.0F, 600.0F, {}, settings);
    CHECK_FALSE(tree->DrawCommands().empty());
    CHECK(tree->HitTest(40.0F, 50.0F) == button);

    tree->PointerMove(40.0F, 50.0F);
    tree->PointerButton(40.0F, 50.0F, Keire::RuntimeUiPointerButton::Primary, true);
    tree->PointerButton(40.0F, 50.0F, Keire::RuntimeUiPointerButton::Primary, false);
    Keire::RuntimeUiEvent event;
    bool clicked = false;
    while (tree->PollEvent(event))
        clicked = clicked || (event.Type == Keire::RuntimeUiEventType::Click && event.Target == button);
    CHECK(clicked);
}

TEST_CASE("retained runtime UI resolves anchors pivots size deltas and local scale against its parent")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto canvas = tree->Create(Keire::RuntimeUiElementType::Canvas);
    const auto panel = tree->Create(Keire::RuntimeUiElementType::Panel, canvas);

    Keire::RuntimeUiStyle canvasStyle;
    canvasStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    canvasStyle.Width = 800.0F;
    canvasStyle.Height = 600.0F;
    REQUIRE(tree->SetStyle(canvas, canvasStyle));

    Keire::RuntimeUiStyle panelStyle;
    panelStyle.Position = Keire::RuntimeUiPositionMode::Absolute;
    panelStyle.UseAnchors = true;
    panelStyle.AnchorMinimum = {0.25F, 0.25F};
    panelStyle.AnchorMaximum = {0.75F, 0.75F};
    panelStyle.Pivot = {0.5F, 0.5F};
    panelStyle.AnchoredPosition = {10.0F, 5.0F};
    panelStyle.SizeDelta = {-20.0F, -40.0F};
    panelStyle.LocalScale = {1.0F, 1.0F};
    REQUIRE(tree->SetStyle(panel, panelStyle));

    Keire::RuntimeUiCanvasSettings settings;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    tree->Layout(800.0F, 600.0F, {}, settings);
    const auto state = tree->State(panel);
    REQUIRE(state);
    CHECK(state->Rect.X == doctest::Approx(220.0F));
    CHECK(state->Rect.Y == doctest::Approx(175.0F));
    CHECK(state->Rect.Width == doctest::Approx(380.0F));
    CHECK(state->Rect.Height == doctest::Approx(260.0F));
}

TEST_CASE("retained vertical and grid layouts place children without fighting Rect Transform anchors")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto vertical = tree->Create(Keire::RuntimeUiElementType::VerticalLayout);
    const auto first = tree->Create(Keire::RuntimeUiElementType::Button, vertical);
    const auto second = tree->Create(Keire::RuntimeUiElementType::Button, vertical);

    Keire::RuntimeUiStyle layout;
    layout.Position = Keire::RuntimeUiPositionMode::Absolute;
    layout.Width = 300.0F;
    layout.Height = 220.0F;
    layout.Padding = {10.0F, 10.0F, 10.0F, 10.0F};
    layout.Gap = 8.0F;
    layout.ControlChildWidth = true;
    layout.ForceExpandWidth = true;
    REQUIRE(tree->SetStyle(vertical, layout));

    Keire::RuntimeUiStyle child;
    child.Position = Keire::RuntimeUiPositionMode::Flow;
    child.Width = 120.0F;
    child.Height = 40.0F;
    REQUIRE(tree->SetStyle(first, child));
    REQUIRE(tree->SetStyle(second, child));

    Keire::RuntimeUiCanvasSettings settings;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    tree->Layout(300.0F, 220.0F, {}, settings);
    const auto firstState = tree->State(first);
    const auto secondState = tree->State(second);
    REQUIRE(firstState);
    REQUIRE(secondState);
    CHECK(firstState->Rect.X == doctest::Approx(10.0F));
    CHECK(firstState->Rect.Width == doctest::Approx(280.0F));
    CHECK(firstState->Rect.Y == doctest::Approx(10.0F));
    CHECK(secondState->Rect.Y == doctest::Approx(58.0F));

    REQUIRE(tree->SetType(vertical, Keire::RuntimeUiElementType::GridLayout));
    layout.GridCellSize = {100.0F, 40.0F};
    layout.Gap = 10.0F;
    REQUIRE(tree->SetStyle(vertical, layout));
    const auto third = tree->Create(Keire::RuntimeUiElementType::Button, vertical);
    REQUIRE(tree->SetStyle(third, child));
    tree->Layout(300.0F, 220.0F, {}, settings);
    const auto thirdState = tree->State(third);
    REQUIRE(thirdState);
    CHECK(thirdState->Rect.X == doctest::Approx(10.0F));
    CHECK(thirdState->Rect.Y == doctest::Approx(60.0F));
}

TEST_CASE("retained buttons render their hover pressed and disabled visual states")
{
    auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto button = tree->Create(Keire::RuntimeUiElementType::Button);
    Keire::RuntimeUiStyle style;
    style.Position = Keire::RuntimeUiPositionMode::Absolute;
    style.Width = 160.0F;
    style.Height = 48.0F;
    style.Background = {0.1F, 0.2F, 0.3F, 1.0F};
    style.HoverBackground = {0.2F, 0.4F, 0.6F, 1.0F};
    style.PressedBackground = {0.05F, 0.1F, 0.2F, 1.0F};
    style.DisabledBackground = {0.15F, 0.15F, 0.15F, 0.5F};
    REQUIRE(tree->SetStyle(button, style));
    REQUIRE(tree->SetInteractable(button, true));

    Keire::RuntimeUiCanvasSettings settings;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    tree->Layout(320.0F, 180.0F, {}, settings);
    tree->PointerMove(20.0F, 20.0F);
    tree->Layout(320.0F, 180.0F, {}, settings);
    const auto hovered = tree->DrawCommands();
    REQUIRE_FALSE(hovered.empty());
    CHECK(hovered.front().ColorValue.Red == doctest::Approx(style.HoverBackground.Red));

    tree->PointerButton(20.0F, 20.0F, Keire::RuntimeUiPointerButton::Primary, true);
    tree->Layout(320.0F, 180.0F, {}, settings);
    CHECK(tree->DrawCommands().front().ColorValue.Red == doctest::Approx(style.PressedBackground.Red));

    REQUIRE(tree->SetEnabled(button, false));
    tree->Layout(320.0F, 180.0F, {}, settings);
    CHECK(tree->DrawCommands().front().ColorValue.Red == doctest::Approx(style.DisabledBackground.Red));
}

TEST_CASE("audio clip assets round-trip deterministic PCM payloads")
{
    Keire::AudioClipData source;
    source.SampleRate = 48'000;
    source.Channels = 2;
    source.Samples = {0.0F, 0.0F, 0.25F, -0.25F, 0.5F, -0.5F};

    const auto encoded = Keire::AudioClipAsset::Encode(source);
    const auto decoded = Keire::AudioClipAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->FrameCount() == 3);
    CHECK(decoded->Clip()->SampleRate == source.SampleRate);
    CHECK(decoded->Clip()->Channels == source.Channels);
    CHECK(decoded->Clip()->Samples == source.Samples);
}

TEST_CASE("audio clip assets preserve encoded streaming payloads without PCM expansion")
{
    Keire::AudioClipData source;
    source.SampleRate = 48'000;
    source.Channels = 2;
    source.Frames = 48'000ULL * 60ULL * 30ULL;
    source.Streaming = true;
    source.EncodedSource = {
        std::byte{0x52}, std::byte{0x49}, std::byte{0x46}, std::byte{0x46},
        std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
    };

    const auto encoded = Keire::AudioClipAsset::Encode(source);
    const auto decoded = Keire::AudioClipAsset::Decode(encoded);
    REQUIRE(decoded);
    CHECK(decoded->FrameCount() == source.Frames);
    CHECK(decoded->Clip()->Streaming);
    CHECK(decoded->Clip()->Samples.empty());
    CHECK(decoded->Clip()->EncodedSource == source.EncodedSource);
}
