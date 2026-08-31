#include <doctest/doctest.h>

#include "Keire/Assets/BuiltinAssetRegistry.h"
#include "Keire/Ui/UiElements.h"
#include "Keire/Ui/UiToolkit.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string_view>
#include <unordered_map>

namespace
{
    class SourceBackedGauge final : public Keire::Ui::VisualElement
    {
      protected:
        [[nodiscard]] std::string_view TypeName() const noexcept override { return "SourceBackedGauge"; }
    };

    class UiMapBindingSource final : public Keire::UiDocumentBindingSource
    {
      public:
        [[nodiscard]] std::any Read(const std::string_view path) const override
        {
            const auto found = Values.find(std::string(path));
            if (found == Values.end())
                throw std::runtime_error("Missing test UI binding path: " + std::string(path));
            return found->second;
        }

        void Write(const std::string_view path, const std::any& value) override
        {
            Values.insert_or_assign(std::string(path), value);
        }

        std::unordered_map<std::string, std::any> Values;
    };

    [[nodiscard]] std::span<const std::byte> AsBytes(const std::string_view value)
    {
        return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
    }

    [[nodiscard]] Keire::AssetId Id(const std::string_view value) { return Keire::AssetId::Parse(value); }

    [[nodiscard]] Keire::UiVisualElementDefinition Element(const Keire::UiVisualElementType type,
                                                           const Keire::AssetId id, std::string name = {})
    {
        Keire::UiVisualElementDefinition result;
        result.Type = type;
        result.StableId = id;
        result.Name = std::move(name);
        return result;
    }

    [[nodiscard]] Keire::Ref<Keire::UiVisualTreeAsset> VisualTree(std::string name,
                                                                  Keire::UiVisualElementDefinition root)
    {
        Keire::UiVisualTreeDefinition definition;
        definition.Name = std::move(name);
        definition.Root = std::move(root);
        return Keire::CreateRef<Keire::UiVisualTreeAsset>(std::move(definition));
    }

    constexpr std::string_view DocumentSource = R"xml(<?xml version="1.0" encoding="utf-8"?>
<ui schemaVersion="1" name="ToolkitTest">
  <style src="10000000-0000-0000-0000-000000000001"/>
  <VisualElement id="20000000-0000-0000-0000-000000000001" name="root" class="screen column">
    <Label id="20000000-0000-0000-0000-000000000002" name="title" class="heading" text="Kéire &amp; UI"/>
    <Button id="20000000-0000-0000-0000-000000000003" name="play" class="primary" text="Play"/>
  </VisualElement>
</ui>
)xml";

    constexpr std::string_view StyleSource = R"css(@keire-style 1;

.screen {
  flex-direction: column;
  width: 640px;
  height: 360px;
  padding: 16px;
  gap: 8px;
  align-items: start;
  --accent: #3366ccff;
}

.screen > .heading {
  font-size: 24px;
  color: #ffffffff;
}

Button.primary {
  width: 160px;
  height: 48px;
  background-color: var(--accent);
}

#play:hover {
  background-color: #ff3300ff;
}
)css";
} // namespace

TEST_CASE("UI visual tree source and cooked codecs are deterministic")
{
    const auto definition = Keire::UiVisualTreeAsset::ParseSource(AsBytes(DocumentSource));
    CHECK(definition.Name == "ToolkitTest");
    REQUIRE(definition.StyleSheets.size() == 1);
    CHECK(definition.Root.Name == "root");
    REQUIRE(definition.Root.Children.size() == 2);
    CHECK(definition.Root.Children[0].Type == Keire::UiVisualElementType::Label);
    CHECK(definition.Root.Children[0].Attributes[0].Value == "Kéire & UI");

    const auto first = Keire::UiVisualTreeAsset::Encode(definition);
    const auto second = Keire::UiVisualTreeAsset::Encode(definition);
    CHECK(first == second);
    const auto decoded = Keire::UiVisualTreeAsset::Decode(first);
    CHECK(decoded->Definition() == definition);
    CHECK(decoded->Find("play") != nullptr);
    CHECK(decoded->Find(Id("20000000-0000-0000-0000-000000000002")) != nullptr);

    const auto source = Keire::UiVisualTreeAsset::EncodeSource(definition);
    const auto reparsed = Keire::UiVisualTreeAsset::ParseSource(source);
    CHECK(reparsed == definition);

    const auto importer = Keire::CreateUiVisualTreeAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport({}, AsBytes(DocumentSource));
    CHECK(imported.Bytes == first);
    CHECK(imported.AssetDependencies == definition.StyleSheets);
}

TEST_CASE("UI stylesheet parses selectors and round trips authoring source")
{
    const auto definition = Keire::UiStyleSheetAsset::ParseSource(AsBytes(StyleSource));
    REQUIRE(definition.Rules.size() == 4);
    CHECK(definition.Rules[0].Specificity == 10);
    CHECK(definition.Rules[1].Specificity == 20);
    CHECK(definition.Rules[2].Specificity == 11);
    CHECK(definition.Rules[3].Specificity == 110);
    CHECK(definition.Rules[1].Parts[1].Combinator == Keire::UiStyleCombinator::Child);

    const auto cooked = Keire::UiStyleSheetAsset::Encode(definition);
    CHECK(Keire::UiStyleSheetAsset::Decode(cooked)->Definition() == definition);
    const auto source = Keire::UiStyleSheetAsset::EncodeSource(definition);
    CHECK(Keire::UiStyleSheetAsset::ParseSource(source) == definition);
}

TEST_CASE("UI document instantiates, queries, styles, lays out, and dispatches events")
{
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(DocumentSource)));
    const auto styleSheet =
        Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(AsBytes(StyleSource)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(
        visualTree, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{styleSheet});

    const auto play = document->Find("play");
    REQUIRE(play);
    CHECK(document->Query({.Type = Keire::UiVisualElementType::Button}).size() == 1);
    CHECK(document->Query({.Class = "heading"}).size() == 1);
    Keire::RuntimeUiCanvasSettings canvas;
    canvas.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    document->Tree()->Layout(640.0F, 360.0F, {}, canvas);
    const auto initial = document->Tree()->State(*play);
    REQUIRE(initial);
    CHECK(initial->Rect.Width == doctest::Approx(160.0F));
    CHECK(initial->Rect.Height == doctest::Approx(48.0F));
    CHECK(initial->Style.Background.Blue == doctest::Approx(0.8F));

    document->SetPseudoState(*play, Keire::UiStylePseudoState::Hover, true);
    const auto hovered = document->Tree()->State(*play);
    REQUIRE(hovered);
    CHECK(hovered->Style.Background.Red == doctest::Approx(1.0F));
    CHECK(hovered->Style.Background.Green == doctest::Approx(0.2F));

    auto scrolledStyle = hovered->Style;
    scrolledStyle.ContentOffset.Y = 40.0F;
    REQUIRE(document->Tree()->SetStyle(*play, std::move(scrolledStyle)));
    document->SetPseudoState(*play, Keire::UiStylePseudoState::Hover, false);
    const auto recascaded = document->Tree()->State(*play);
    REQUIRE(recascaded);
    CHECK(recascaded->Style.ContentOffset.Y == doctest::Approx(40.0F));

    const auto centerX = recascaded->Rect.X + recascaded->Rect.Width * 0.5F;
    const auto centerY = recascaded->Rect.Y + recascaded->Rect.Height * 0.5F;
    CHECK(document->Tree()->PointerButton(centerX, centerY, Keire::RuntimeUiPointerButton::Primary, true));
    CHECK(document->Tree()->PointerButton(centerX, centerY, Keire::RuntimeUiPointerButton::Primary, false));
    Keire::RuntimeUiEvent event;
    bool clicked = false;
    while (document->Tree()->PollEvent(event))
        clicked = clicked || event.Type == Keire::RuntimeUiEventType::Click;
    CHECK(clicked);
}

TEST_CASE("Unstyled retained VisualElements use Unity-compatible column flow")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="DefaultColumn">
  <VisualElement id="20100000-0000-0000-0000-000000000001" style="width: 200; height: 100;">
    <Label id="20100000-0000-0000-0000-000000000002" name="first" text="First" style="width: 100; height: 20;"/>
    <Label id="20100000-0000-0000-0000-000000000003" name="second" text="Second" style="width: 100; height: 20;"/>
  </VisualElement>
</ui>)xml";
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(visualTree);
    document->Tree()->Layout(200.0F, 100.0F);
    const auto root = document->Tree()->State(document->Root());
    const auto first = document->Tree()->State(*document->Find("first"));
    const auto second = document->Tree()->State(*document->Find("second"));
    REQUIRE(root);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(root->Type == Keire::RuntimeUiElementType::VerticalLayout);
    CHECK(second->Rect.Y == doctest::Approx(first->Rect.Y + first->Rect.Height));
}

TEST_CASE("UI documents can share a presentation tree and release only their own subtree")
{
    const auto definition = Keire::UiVisualTreeAsset::ParseSource(AsBytes(DocumentSource));
    const auto visualTree = Keire::CreateRef<Keire::UiVisualTreeAsset>(definition);
    const auto shared = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto host = shared->Create(Keire::RuntimeUiElementType::Canvas);
    Keire::RuntimeUiElementId firstRoot;
    Keire::RuntimeUiElementId secondRoot;
    {
        const auto first = Keire::CreateRef<Keire::UiDocument>(
            visualTree, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{}, shared, host);
        const auto second = Keire::CreateRef<Keire::UiDocument>(
            visualTree, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{}, shared, host);
        firstRoot = first->Root();
        secondRoot = second->Root();
        CHECK(shared->Exists(firstRoot));
        CHECK(shared->Exists(secondRoot));
        CHECK(shared->Children(host).size() == 2);
    }
    CHECK_FALSE(shared->Exists(firstRoot));
    CHECK_FALSE(shared->Exists(secondRoot));
    CHECK(shared->Exists(host));
}

TEST_CASE("UI document layouts reuse unchanged work and invalidate edited leaves")
{
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(DocumentSource)));
    const auto styleSheet =
        Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(AsBytes(StyleSource)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(
        visualTree, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{styleSheet});
    Keire::RuntimeUiCanvasSettings canvas;
    canvas.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    document->Tree()->Layout(640.0F, 360.0F, {}, canvas);
    const auto first = document->Tree()->Statistics();
    const auto firstCommands = document->Tree()->DrawCommands().size();
    CHECK(first.LayoutPasses == 1);
    CHECK(first.DirtyElements == 0);

    document->Tree()->Layout(640.0F, 360.0F, {}, canvas);
    const auto reused = document->Tree()->Statistics();
    CHECK(reused.LayoutPasses == first.LayoutPasses);
    CHECK(reused.ReusedLayoutPasses == first.ReusedLayoutPasses + 1);
    CHECK(document->Tree()->DrawCommands().size() == firstCommands);

    const auto title = document->Find("title");
    REQUIRE(title);
    auto titleState = document->Tree()->State(*title);
    REQUIRE(titleState);
    titleState->Style.FontSize += 1.0F;
    REQUIRE(document->Tree()->SetStyle(*title, titleState->Style));
    CHECK(document->Tree()->Statistics().DirtyElements > 0);
    document->Tree()->Layout(640.0F, 360.0F, {}, canvas);
    const auto changed = document->Tree()->Statistics();
    CHECK(changed.LayoutPasses == reused.LayoutPasses + 1);
    CHECK(changed.DirtyElements == 0);
}

TEST_CASE("Runtime UI reports bounded dirty reasons and deterministic event routes")
{
    const auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto root = tree->Create(Keire::RuntimeUiElementType::VerticalLayout);
    const auto parent = tree->Create(Keire::RuntimeUiElementType::Panel, root);
    const auto button = tree->Create(Keire::RuntimeUiElementType::Button, parent);
    REQUIRE(tree->SetInteractable(button, true));
    tree->Layout(320.0F, 180.0F);

    auto content = tree->State(button)->Content;
    content.Text = "Ready";
    REQUIRE(tree->SetContent(button, std::move(content)));
    CHECK(tree->DirtyReasons(button) == Keire::RuntimeUiDirtyReason::Content);
    CHECK(tree->DirtyReasons(parent) == Keire::RuntimeUiDirtyReason::Descendant);
    CHECK(tree->DirtyReasons(root) == Keire::RuntimeUiDirtyReason::Descendant);

    tree->Layout(320.0F, 180.0F);
    auto style = tree->State(button)->Style;
    style.Opacity = 0.75F;
    REQUIRE(tree->SetStyle(button, style));
    CHECK(tree->DirtyReasons(button) == Keire::RuntimeUiDirtyReason::Style);

    REQUIRE(tree->DispatchEvent(
        {.Type = Keire::RuntimeUiEventType::Click, .Target = button, .PointerX = 12.0F, .PointerY = 18.0F}));
    const auto route = tree->EventRouteHistory();
    REQUIRE(route.size() == 5);
    CHECK(route[0].Phase == Keire::RuntimeUiEventPhase::TrickleDown);
    CHECK(route[0].CurrentTarget == root);
    CHECK(route[1].Phase == Keire::RuntimeUiEventPhase::TrickleDown);
    CHECK(route[1].CurrentTarget == parent);
    CHECK(route[2].Phase == Keire::RuntimeUiEventPhase::Target);
    CHECK(route[2].CurrentTarget == button);
    CHECK(route[3].Phase == Keire::RuntimeUiEventPhase::BubbleUp);
    CHECK(route[3].CurrentTarget == parent);
    CHECK(route[4].Phase == Keire::RuntimeUiEventPhase::BubbleUp);
    CHECK(route[4].CurrentTarget == root);
    CHECK(std::ranges::all_of(route, [&](const auto& entry)
                              { return entry.Sequence == route.front().Sequence && entry.Target == button; }));
}

TEST_CASE("UI document exposes selector precedence and separate style and repaint timings")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="SelectorTrace">
  <Button id="40100000-0000-0000-0000-000000000001" name="action" class="primary" style="width: 50;"/>
</ui>)xml";
    constexpr std::string_view styles = R"css(@keire-style 1;
Button { opacity: 0.5; width: 10; }
.primary { width: 20; }
Button.primary { height: 30; }
#action { width: 40; }
)css";
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto styleSheet =
        Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(AsBytes(styles)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(
        visualTree, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{styleSheet});
    const auto action = document->Find("action");
    REQUIRE(action);

    const auto trace = document->ResolvedStyleTrace(*action);
    REQUIRE(trace.size() == 5);
    CHECK(trace[0].Selector == "Button");
    CHECK(trace[0].AppliedProperties == std::vector<std::string>{"opacity"});
    CHECK(trace[1].Selector == ".primary");
    CHECK(trace[1].AppliedProperties.empty());
    CHECK(trace[2].Selector == "Button.primary");
    CHECK(trace[2].AppliedProperties == std::vector<std::string>{"height"});
    CHECK(trace[3].Selector == "#action");
    CHECK(trace[3].AppliedProperties.empty());
    CHECK(trace[4].Selector == "<inline>");
    CHECK(trace[4].AppliedProperties == std::vector<std::string>{"width"});
    CHECK(trace[4].Specificity > trace[3].Specificity);

    auto statistics = document->Tree()->Statistics();
    CHECK(statistics.StylePasses == 1);
    CHECK(statistics.StyleMilliseconds >= 0.0F);
    document->Tree()->ReportRepaintPass(0.25F);
    statistics = document->Tree()->Statistics();
    CHECK(statistics.RepaintPasses == 1);
    CHECK(statistics.RepaintMilliseconds == doctest::Approx(0.25F));
    CHECK_THROWS_AS(document->Tree()->ReportRepaintPass(-0.1F), std::invalid_argument);
}

TEST_CASE("UI document maps scroll-view content extent and wheel sensitivity")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="ScrollControl">
  <ScrollView id="40200000-0000-0000-0000-000000000001" name="scroll" interactable="true" content-width="640" content-height="1200" scroll-sensitivity="24"/>
</ui>)xml";
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(visualTree);
    const auto scroll = document->Find("scroll");
    REQUIRE(scroll);
    const auto state = document->Tree()->State(*scroll);
    REQUIRE(state);
    CHECK(state->Interactable);
    CHECK(state->Control.ContentSize.X == doctest::Approx(640.0F));
    CHECK(state->Control.ContentSize.Y == doctest::Approx(1200.0F));
    CHECK(state->Control.ScrollSensitivity == doctest::Approx(24.0F));
}

TEST_CASE("Runtime UI records the independent layout scale used by each document root")
{
    const auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto constantRoot = tree->Create(Keire::RuntimeUiElementType::Panel);
    const auto scaledRoot = tree->Create(Keire::RuntimeUiElementType::Panel);
    const auto constantScroll = tree->Create(Keire::RuntimeUiElementType::ScrollView, constantRoot);
    const auto scaledScroll = tree->Create(Keire::RuntimeUiElementType::ScrollView, scaledRoot);

    Keire::RuntimeUiCanvasSettings constant;
    constant.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    Keire::RuntimeUiCanvasSettings scaled;
    scaled.ScaleMode = Keire::RuntimeUiScaleMode::ScaleWithViewport;
    scaled.ReferenceWidth = 640.0F;
    scaled.ReferenceHeight = 480.0F;
    REQUIRE(tree->SetRootCanvasSettings(constantRoot, constant));
    REQUIRE(tree->SetRootCanvasSettings(scaledRoot, scaled));
    tree->Layout(320.0F, 240.0F);

    const auto constantState = tree->State(constantScroll);
    const auto scaledState = tree->State(scaledScroll);
    REQUIRE(constantState);
    REQUIRE(scaledState);
    CHECK(constantState->LayoutScale == doctest::Approx(1.0F));
    CHECK(scaledState->LayoutScale == doctest::Approx(0.5F));
}

TEST_CASE("Source-backed UI documents instantiate custom controls and resolve authored bindings explicitly")
{
    (void)Keire::Ui::UxmlElementRegistry::Register(
        {.Name = "SourceBackedGauge", .Factory = [] { return Keire::CreateRef<SourceBackedGauge>(); }});
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="SourceBacked">
  <SourceBackedGauge id="42400000-0000-0000-0000-000000000001" name="gauge">
    <Label id="42400000-0000-0000-0000-000000000002" name="status" bind:text="Telemetry.Status"/>
  </SourceBackedGauge>
</ui>)xml";
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(visualTree);

    REQUIRE(document->VisualRoot());
    CHECK(dynamic_cast<SourceBackedGauge*>(document->VisualRoot().Get()) != nullptr);
    const auto statusVisual = document->Visual("status");
    REQUIRE(statusVisual);
    REQUIRE(statusVisual->AuthoredBindings().size() == 1);
    CHECK(statusVisual->AuthoredBindings().front().Path == "Telemetry.Status");
    REQUIRE(document->BindingDiagnostics().size() == 1);
    CHECK(document->BindingDiagnostics().front().Code == "UiBindingSourceUnavailable");
    CHECK(document->BindingDiagnostics().front().Element == statusVisual->StableId());

    const auto bindingSource = Keire::CreateRef<UiMapBindingSource>();
    bindingSource->Values.emplace("Telemetry.Status", std::string("Ready"));
    document->SetBindingSource(bindingSource);
    CHECK(document->BindingDiagnostics().empty());
    const auto status = document->Find("status");
    REQUIRE(status);
    REQUIRE(document->Tree()->State(*status));
    CHECK(document->Tree()->State(*status)->Content.Text == "Ready");

    bindingSource->Values.insert_or_assign("Telemetry.Status", std::string("Running"));
    document->UpdateBindings();
    CHECK(document->Tree()->State(*status)->Content.Text == "Running");
}

TEST_CASE("Runtime UI default styles validate and transitions advance without warming unchanged trees")
{
    const auto tree = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto root = tree->Create(Keire::RuntimeUiElementType::Panel);
    Keire::RuntimeUiStyle initial;
    initial.Width = 10.0F;
    initial.Height = 20.0F;
    initial.Opacity = 0.0F;
    initial.Background = {0.0F, 0.0F, 0.0F, 1.0F};
    initial.TransitionProperties[0] = Keire::RuntimeUiTransitionProperty::Opacity;
    initial.TransitionProperties[1] = Keire::RuntimeUiTransitionProperty::Width;
    initial.TransitionPropertyCount = 2;
    initial.TransitionDurations[0] = 2.0F;
    initial.TransitionDurations[1] = 4.0F;
    initial.TransitionDurationCount = 2;
    bool styleAccepted = false;
    CHECK_NOTHROW(styleAccepted = tree->SetStyle(root, initial));
    REQUIRE(styleAccepted);
    tree->Layout(100.0F, 100.0F);
    const auto before = tree->Statistics();

    auto target = initial;
    target.Width = 30.0F;
    target.Opacity = 1.0F;
    REQUIRE(tree->SetStyle(root, target));
    auto state = tree->State(root);
    REQUIRE(state);
    CHECK(state->Style.Width == doctest::Approx(10.0F));
    CHECK(state->Style.Opacity == doctest::Approx(0.0F));
    REQUIRE(tree->AdvanceTransitions(1.0F));
    state = tree->State(root);
    REQUIRE(state);
    CHECK(state->Style.Width == doctest::Approx(15.0F));
    CHECK(state->Style.Opacity == doctest::Approx(0.5F));
    tree->Layout(100.0F, 100.0F);
    CHECK(tree->Statistics().LayoutPasses == before.LayoutPasses + 1);

    CHECK_FALSE(tree->AdvanceTransitions(0.0F));
    const auto stable = tree->Statistics();
    tree->Layout(100.0F, 100.0F);
    CHECK(tree->Statistics().LayoutPasses == stable.LayoutPasses);
    CHECK(tree->Statistics().ReusedLayoutPasses == stable.ReusedLayoutPasses + 1);

    const auto rollback = tree->State(root);
    REQUIRE(rollback);
    CHECK_THROWS_AS((void)tree->AdvanceTransitions(-0.1F), std::invalid_argument);
    auto invalid = target;
    invalid.TransitionDurations[0] = 61.0F;
    CHECK_THROWS_AS((void)tree->SetStyle(root, invalid), std::invalid_argument);
    CHECK(tree->State(root)->Style == rollback->Style);

    REQUIRE(tree->AdvanceTransitions(3.0F));
    state = tree->State(root);
    REQUIRE(state);
    CHECK(state->Style.Width == doctest::Approx(30.0F));
    CHECK(state->Style.Opacity == doctest::Approx(1.0F));
    CHECK_FALSE(tree->AdvanceTransitions(1.0F));
}

TEST_CASE("UI document cascade starts parsed pseudo-state transitions deterministically")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="TransitionCascade">
  <Button id="42500000-0000-0000-0000-000000000001" name="button"/>
</ui>)xml";
    constexpr std::string_view styles = R"css(@keire-style 1;
#button { width: 100; height: 40; background-color: #000000ff; transition-property: background-color, opacity; transition-duration: 200ms, 1s; }
#button:hover { background-color: #ffffffff; opacity: 0.5; }
)css";
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto styleSheet =
        Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(AsBytes(styles)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(
        visualTree, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{styleSheet});
    const auto button = document->Find("button");
    REQUIRE(button);
    auto state = document->Tree()->State(*button);
    REQUIRE(state);
    CHECK(state->Style.TransitionPropertyCount == 2);
    CHECK(state->Style.TransitionDurationCount == 2);
    CHECK(state->Style.TransitionDurations[0] == doctest::Approx(0.2F));
    CHECK(state->Style.TransitionDurations[1] == doctest::Approx(1.0F));

    document->SetPseudoState(*button, Keire::UiStylePseudoState::Hover, true);
    state = document->Tree()->State(*button);
    REQUIRE(state);
    CHECK(state->Style.Background.Red == doctest::Approx(0.0F));
    CHECK(state->Style.Opacity == doctest::Approx(1.0F));
    REQUIRE(document->Tree()->AdvanceTransitions(0.1F));
    state = document->Tree()->State(*button);
    REQUIRE(state);
    CHECK(state->Style.Background.Red == doctest::Approx(0.5F));
    CHECK(state->Style.Opacity == doctest::Approx(0.95F));
    REQUIRE(document->Tree()->AdvanceTransitions(0.9F));
    state = document->Tree()->State(*button);
    REQUIRE(state);
    CHECK(state->Style.Background.Red == doctest::Approx(1.0F));
    CHECK(state->Style.Opacity == doctest::Approx(0.5F));
}

TEST_CASE("UI documents parse bounded gradients and lower immutable draw values")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="Gradients">
  <VisualElement id="43000000-0000-0000-0000-000000000001" name="linear" style="width: 200; height: 100; opacity: 0.5; background: linear-gradient(90deg, #ff0000ff 0%, #0000ffff 100%);">
    <VisualElement id="43000000-0000-0000-0000-000000000002" name="radial" style="width: 50; height: 50; background-image: radial-gradient(circle 75% at 25% 60%, #ffffffff 0%, #00000000 100%);"/>
  </VisualElement>
</ui>)xml";
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(visualTree);
    const auto linear = document->Tree()->State(document->Root());
    const auto radialId = document->Find("radial");
    REQUIRE(linear);
    REQUIRE(radialId);
    const auto radial = document->Tree()->State(*radialId);
    REQUIRE(radial);
    CHECK(linear->Style.BackgroundGradient.Kind == Keire::RuntimeUiGradientKind::Linear);
    CHECK(linear->Style.BackgroundGradient.LinearAngleDegrees == doctest::Approx(90.0F));
    CHECK(linear->Style.BackgroundGradient.StopCount == 2);
    CHECK(radial->Style.BackgroundGradient.Kind == Keire::RuntimeUiGradientKind::Radial);
    CHECK(radial->Style.BackgroundGradient.RadialCenter.X == doctest::Approx(0.25F));
    CHECK(radial->Style.BackgroundGradient.RadialCenter.Y == doctest::Approx(0.6F));
    CHECK(radial->Style.BackgroundGradient.RadialRadius == doctest::Approx(0.75F));

    document->Tree()->Layout(200.0F, 100.0F);
    const auto linearDraw =
        std::ranges::find_if(document->Tree()->DrawCommands(),
                             [&](const Keire::RuntimeUiDrawCommand& command)
                             {
                                 return command.Element == document->Root() &&
                                        command.BackgroundGradient.Kind == Keire::RuntimeUiGradientKind::Linear;
                             });
    REQUIRE(linearDraw != document->Tree()->DrawCommands().end());
    CHECK(linearDraw->BackgroundGradient.StopCount == 2);
    CHECK(linearDraw->BackgroundGradient.Stops[0].ColorValue.Alpha == doctest::Approx(0.5F));
    CHECK(linearDraw->BackgroundGradient.Stops[1].Offset == doctest::Approx(1.0F));

    constexpr std::string_view unsorted = R"xml(<ui schemaVersion="1" name="BadGradient">
  <VisualElement id="43000000-0000-0000-0000-000000000003" style="background: linear-gradient(0deg, #ffffffff 80%, #000000ff 20%);"/>
</ui>)xml";
    const auto invalid =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(unsorted)));
    CHECK_THROWS_WITH_AS((void)Keire::CreateRef<Keire::UiDocument>(invalid),
                         doctest::Contains("offsets must be sorted"), std::runtime_error);
}

TEST_CASE("UI visual tree importer publishes image and font dependencies but not logical render targets")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="Dependencies">
  <style src="60000000-0000-0000-0000-000000000001"/>
  <VisualElement id="60000000-0000-0000-0000-000000000002" image="60000000-0000-0000-0000-000000000003" font="60000000-0000-0000-0000-000000000004">
    <TemplateContainer id="60000000-0000-0000-0000-000000000005" template="60000000-0000-0000-0000-000000000006"/>
    <Image id="60000000-0000-0000-0000-000000000007" render-texture="60000000-0000-0000-0000-000000000008"/>
  </VisualElement>
</ui>)xml";
    const auto importer = Keire::CreateUiVisualTreeAssetImporter();
    REQUIRE(importer.ContextualImport);
    const auto imported = importer.ContextualImport({}, AsBytes(source));
    const std::vector expected{Id("60000000-0000-0000-0000-000000000001"), Id("60000000-0000-0000-0000-000000000003"),
                               Id("60000000-0000-0000-0000-000000000004"), Id("60000000-0000-0000-0000-000000000006")};
    CHECK(imported.AssetDependencies == expected);
    CHECK(std::ranges::find(imported.AssetDependencies, Id("60000000-0000-0000-0000-000000000008")) ==
          imported.AssetDependencies.end());

    constexpr std::string_view renderTextureSource = R"xml(<ui schemaVersion="1" name="RenderTextureImage">
  <Image id="60000000-0000-0000-0000-000000000009" render-texture="60000000-0000-0000-0000-000000000008" style="width: 64; height: 64;"/>
</ui>)xml";
    const auto renderTextureTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(renderTextureSource)));
    const auto renderTextureDocument = Keire::CreateRef<Keire::UiDocument>(renderTextureTree);
    renderTextureDocument->Tree()->Layout(64.0F, 64.0F);
    const auto imageCommand = std::ranges::find(renderTextureDocument->Tree()->DrawCommands(),
                                                Keire::RuntimeUiDrawType::Image, &Keire::RuntimeUiDrawCommand::Type);
    REQUIRE(imageCommand != renderTextureDocument->Tree()->DrawCommands().end());
    CHECK_FALSE(imageCommand->Asset);
    CHECK(imageCommand->RenderTexture == Id("60000000-0000-0000-0000-000000000008"));

    constexpr std::string_view malformed = R"xml(<ui schemaVersion="1" name="MalformedDependency">
  <Image id="61000000-0000-0000-0000-000000000001" image="not-an-asset-id"/>
</ui>)xml";
    CHECK_THROWS_AS((void)importer.ContextualImport({}, AsBytes(malformed)), std::invalid_argument);
    constexpr std::string_view ambiguous = R"xml(<ui schemaVersion="1" name="AmbiguousDependency">
  <Image id="61000000-0000-0000-0000-000000000002" image="61000000-0000-0000-0000-000000000003" render-texture="61000000-0000-0000-0000-000000000004"/>
</ui>)xml";
    CHECK_THROWS_WITH_AS((void)importer.ContextualImport({}, AsBytes(ambiguous)),
                         doctest::Contains("both an asset image and logical render texture"), std::invalid_argument);
}

TEST_CASE("UI document construction rolls back shared-tree nodes when style resolution fails")
{
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(DocumentSource)));
    constexpr std::string_view invalidStyle = R"css(@keire-style 1;
VisualElement { unsupported-production-property: 1; }
)css";
    const auto styleSheet =
        Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(AsBytes(invalidStyle)));
    const auto shared = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto host = shared->Create(Keire::RuntimeUiElementType::Canvas);
    CHECK_THROWS_WITH_AS(
        (void)Keire::CreateRef<Keire::UiDocument>(
            visualTree, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{styleSheet}, shared, host),
        doctest::Contains("unsupported runtime property"), std::runtime_error);
    CHECK(shared->Exists(host));
    CHECK(shared->Children(host).empty());
    CHECK(shared->Statistics().Elements == 1);
}

TEST_CASE("UI document root accepts Builder-authored full-parent percentage dimensions")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="FullParent">
  <VisualElement id="40000000-0000-0000-0000-000000000001" name="root" style="width: 100%; height: 100%;"/>
</ui>)xml";
    const auto tree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(tree);
    Keire::RuntimeUiCanvasSettings canvas;
    canvas.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    document->Tree()->Layout(800.0F, 450.0F, {}, canvas);
    const auto root = document->Tree()->State(document->Root());
    REQUIRE(root);
    CHECK(root->Style.WidthPercent == doctest::Approx(1.0F));
    CHECK(root->Style.HeightPercent == doctest::Approx(1.0F));
    CHECK(root->Rect.Width == doctest::Approx(800.0F));
    CHECK(root->Rect.Height == doctest::Approx(450.0F));
}

TEST_CASE("UI document maps retained flex and percentage styles deterministically")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="FlexPercent">
  <VisualElement id="41000000-0000-0000-0000-000000000001" name="root" style="width: 80%; height: 75%; min-width: 25%; min-height: 20%; max-width: 90%; max-height: 85%; flex-direction: row-reverse; flex-wrap: wrap-reverse; justify-content: space-between; align-items: center; overflow: clip;">
    <VisualElement id="41000000-0000-0000-0000-000000000002" name="child" style="position: absolute; left: 10%; top: 15%; width: 40%; height: 30%; flex-grow: 2; flex-shrink: 3; align-self: flex-end;"/>
  </VisualElement>
</ui>)xml";
    const auto tree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(tree);
    const auto root = document->Tree()->State(document->Root());
    REQUIRE(root);
    CHECK(root->Type == Keire::RuntimeUiElementType::HorizontalLayout);
    CHECK(root->Style.WidthPercent == doctest::Approx(0.8F));
    CHECK(root->Style.HeightPercent == doctest::Approx(0.75F));
    CHECK(root->Style.MinimumWidthPercent == doctest::Approx(0.25F));
    CHECK(root->Style.MinimumHeightPercent == doctest::Approx(0.2F));
    CHECK(root->Style.MaximumWidthPercent == doctest::Approx(0.9F));
    CHECK(root->Style.MaximumHeightPercent == doctest::Approx(0.85F));
    CHECK(root->Style.ReverseChildren);
    CHECK(root->Style.Wrap == Keire::RuntimeUiWrapMode::WrapReverse);
    CHECK(root->Style.JustifyContent == Keire::RuntimeUiJustification::SpaceBetween);
    CHECK(root->Style.ChildHorizontalAlignment == Keire::RuntimeUiAlignment::Center);
    CHECK(root->Style.ChildVerticalAlignment == Keire::RuntimeUiAlignment::Center);
    CHECK(root->Style.ClipChildren);

    const auto childId = document->Find("child");
    REQUIRE(childId);
    const auto child = document->Tree()->State(*childId);
    REQUIRE(child);
    CHECK(child->Style.Position == Keire::RuntimeUiPositionMode::Absolute);
    CHECK(child->Style.XPercent == doctest::Approx(0.1F));
    CHECK(child->Style.YPercent == doctest::Approx(0.15F));
    CHECK(child->Style.WidthPercent == doctest::Approx(0.4F));
    CHECK(child->Style.HeightPercent == doctest::Approx(0.3F));
    CHECK(child->Style.FlexGrow == doctest::Approx(2.0F));
    CHECK(child->Style.FlexShrink == doctest::Approx(3.0F));
    CHECK(child->Style.HasAlignSelf);
    CHECK(child->Style.AlignSelf == Keire::RuntimeUiAlignment::End);

    Keire::RuntimeUiCanvasSettings canvas;
    canvas.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    document->Tree()->Layout(1000.0F, 800.0F, {}, canvas);
    const auto laidOutRoot = document->Tree()->State(document->Root());
    const auto laidOutChild = document->Tree()->State(*childId);
    REQUIRE(laidOutRoot);
    REQUIRE(laidOutChild);
    CHECK(laidOutRoot->Rect.Width == doctest::Approx(800.0F));
    CHECK(laidOutRoot->Rect.Height == doctest::Approx(600.0F));
    CHECK(laidOutChild->Rect.X == doctest::Approx(80.0F));
    CHECK(laidOutChild->Rect.Y == doctest::Approx(90.0F));
    CHECK(laidOutChild->Rect.Width == doctest::Approx(320.0F));
    CHECK(laidOutChild->Rect.Height == doctest::Approx(180.0F));
}

TEST_CASE("UI document maps text alignment and safely renders collapsed slider ranges")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="CollapsedSlider">
  <VisualElement id="42000000-0000-0000-0000-000000000001" name="root" style="width: 640px; height: 360px;">
    <Label id="42000000-0000-0000-0000-000000000002" name="label" text="Centered" style="width: 240px; height: 40px; text-align: center; vertical-align: end;"/>
    <Slider id="42000000-0000-0000-0000-000000000003" name="slider" minimum="5" maximum="5" value="5" style="width: 280px; height: 32px; background-color: #25364aff; color: #4da3ffff;"/>
  </VisualElement>
</ui>)xml";
    const auto tree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(tree);
    Keire::RuntimeUiCanvasSettings canvas;
    canvas.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    document->Tree()->Layout(640.0F, 360.0F, {}, canvas);

    const auto label = document->Find("label");
    REQUIRE(label);
    const auto labelState = document->Tree()->State(*label);
    REQUIRE(labelState);
    CHECK(labelState->Style.HorizontalAlignment == Keire::RuntimeUiAlignment::Center);
    CHECK(labelState->Style.VerticalAlignment == Keire::RuntimeUiAlignment::End);

    for (const auto& command : document->Tree()->DrawCommands())
    {
        CHECK(std::isfinite(command.Rect.X));
        CHECK(std::isfinite(command.Rect.Y));
        CHECK(std::isfinite(command.Rect.Width));
        CHECK(std::isfinite(command.Rect.Height));
    }
}

TEST_CASE("UI document rejects unsupported retained style values")
{
    constexpr std::string_view invalidPercentage = R"xml(<ui schemaVersion="1" name="InvalidPercentage">
  <VisualElement id="42000000-0000-0000-0000-000000000001" style="width: 101%;"/>
</ui>)xml";
    const auto percentageTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(invalidPercentage)));
    CHECK_THROWS_WITH_AS((void)Keire::CreateRef<Keire::UiDocument>(percentageTree),
                         doctest::Contains("between 0% and 100%"), std::runtime_error);

    constexpr std::string_view invalidFlex = R"xml(<ui schemaVersion="1" name="InvalidFlex">
  <VisualElement id="42000000-0000-0000-0000-000000000002" style="flex-wrap: balance;"/>
</ui>)xml";
    const auto flexTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(invalidFlex)));
    CHECK_THROWS_WITH_AS((void)Keire::CreateRef<Keire::UiDocument>(flexTree), doctest::Contains("flex-wrap property"),
                         std::runtime_error);
}

TEST_CASE("UI templates expand nested assets with named and default slot content deterministically")
{
    const Keire::AssetId cardId(0x5000000000000000ULL, 1);
    const Keire::AssetId wrapperId(0x5000000000000000ULL, 2);
    auto cardRoot =
        Element(Keire::UiVisualElementType::VisualElement, Keire::AssetId(0x5100000000000000ULL, 1), "card");
    auto defaultSlot = Element(Keire::UiVisualElementType::Slot, Keire::AssetId(0x5100000000000000ULL, 2));
    defaultSlot.Children.push_back(
        Element(Keire::UiVisualElementType::Label, Keire::AssetId(0x5100000000000000ULL, 3), "default-fallback"));
    auto actionSlot = Element(Keire::UiVisualElementType::Slot, Keire::AssetId(0x5100000000000000ULL, 4), "actions");
    actionSlot.Children.push_back(
        Element(Keire::UiVisualElementType::Button, Keire::AssetId(0x5100000000000000ULL, 5), "action-fallback"));
    cardRoot.Children = {std::move(defaultSlot), std::move(actionSlot)};
    const auto card = VisualTree("CardTemplate", std::move(cardRoot));

    auto wrapperRoot =
        Element(Keire::UiVisualElementType::TemplateContainer, Keire::AssetId(0x5200000000000000ULL, 1), "nested-card");
    wrapperRoot.Template = cardId;
    auto body = Element(Keire::UiVisualElementType::Label, Keire::AssetId(0x5200000000000000ULL, 2), "supplied-body");
    auto action =
        Element(Keire::UiVisualElementType::Button, Keire::AssetId(0x5200000000000000ULL, 3), "supplied-action");
    action.Slot = "actions";
    wrapperRoot.Children = {std::move(body), std::move(action)};
    const auto wrapper = VisualTree("WrapperTemplate", std::move(wrapperRoot));

    auto documentRoot = Element(Keire::UiVisualElementType::TemplateContainer, Keire::AssetId(0x5300000000000000ULL, 1),
                                "document-root");
    documentRoot.Template = wrapperId;
    const auto source = VisualTree("NestedTemplateDocument", std::move(documentRoot));
    const std::unordered_map<Keire::AssetId, Keire::Ref<const Keire::UiVisualTreeAsset>> templates{
        {cardId, card}, {wrapperId, wrapper}};
    const auto resolver = [&templates](const Keire::AssetId id)
    {
        const auto found = templates.find(id);
        return found == templates.end() ? Keire::Ref<const Keire::UiVisualTreeAsset>{} : found->second;
    };

    const auto first = Keire::CreateRef<Keire::UiDocument>(
        source, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{}, resolver);
    const auto second = Keire::CreateRef<Keire::UiDocument>(
        source, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{}, resolver);
    CHECK(first->Find("supplied-body").has_value());
    CHECK(first->Find("supplied-action").has_value());
    CHECK_FALSE(first->Find("default-fallback").has_value());
    CHECK_FALSE(first->Find("action-fallback").has_value());
    CHECK(first->Query({.Type = Keire::UiVisualElementType::Slot}).empty());
    CHECK(first->Query({}).size() == second->Query({}).size());
    REQUIRE(first->Find("card"));
    REQUIRE(second->Find("card"));
    CHECK(first->Tree()->Children(*first->Find("card")).size() ==
          second->Tree()->Children(*second->Find("card")).size());
}

TEST_CASE("UI template roots retain stylesheet dimensions inside non-stretching hosts")
{
    const auto templateId = Id("50000000-0000-4000-8000-000000000101");
    auto cardRoot =
        Element(Keire::UiVisualElementType::VisualElement, Id("50000000-0000-4000-8000-000000000102"), "card");
    cardRoot.Classes = {"card"};
    const auto card = VisualTree("Card", std::move(cardRoot));

    auto screenRoot =
        Element(Keire::UiVisualElementType::VisualElement, Id("50000000-0000-4000-8000-000000000103"), "screen");
    screenRoot.Classes = {"screen"};
    auto host =
        Element(Keire::UiVisualElementType::TemplateContainer, Id("50000000-0000-4000-8000-000000000104"), "host");
    host.Template = templateId;
    screenRoot.Children.push_back(std::move(host));
    const auto source = VisualTree("Screen", std::move(screenRoot));
    constexpr std::string_view styles = R"css(@keire-style 1;
.screen { width: 1920px; height: 1080px; flex-direction: column; align-items: start; }
.card { width: 560px; height: 320px; }
)css";
    const auto styleSheet =
        Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(AsBytes(styles)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(
        source, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{styleSheet},
        [=](const Keire::AssetId id) -> Keire::Ref<const Keire::UiVisualTreeAsset>
        { return id == templateId ? card : Keire::Ref<const Keire::UiVisualTreeAsset>{}; });
    document->Tree()->Layout(1920.0F, 1080.0F);

    const auto cardState = document->Tree()->State(*document->Find("card"));
    REQUIRE(cardState);
    CHECK(cardState->Rect.Width == doctest::Approx(560.0F));
    CHECK(cardState->Rect.Height == doctest::Approx(320.0F));
}

TEST_CASE("Checked retained toggles preserve their label surface and bound their indicator")
{
    constexpr std::string_view source = R"xml(<ui schemaVersion="1" name="ToggleIndicator">
  <Toggle id="50000000-0000-4000-8000-000000000111" name="toggle" text="Debug Overlay" checked="true"
          style="width: 220; height: 48; background-color: #263449ff; color: #edf5ffff;"/>
</ui>)xml";
    const auto visualTree =
        Keire::CreateRef<Keire::UiVisualTreeAsset>(Keire::UiVisualTreeAsset::ParseSource(AsBytes(source)));
    const auto document = Keire::CreateRef<Keire::UiDocument>(visualTree);
    document->Tree()->Layout(220.0F, 48.0F);

    const auto toggle = document->Find("toggle");
    REQUIRE(toggle);
    const auto state = document->Tree()->State(*toggle);
    REQUIRE(state);
    std::vector<Keire::RuntimeUiDrawCommand> quads;
    std::ranges::copy_if(document->Tree()->DrawCommands(), std::back_inserter(quads), [toggle](const auto& command)
                         { return command.Element == *toggle && command.Type == Keire::RuntimeUiDrawType::Quad; });
    REQUIRE(quads.size() == 2);
    CHECK(quads[0].Rect.Width == doctest::Approx(state->Rect.Width));
    CHECK(quads[1].Rect.Width <= 22.0F);
    CHECK(quads[1].Rect.Width < state->Rect.Width * 0.5F);
}

TEST_CASE("UI template slots instantiate fallback content when no insertion is supplied")
{
    const Keire::AssetId templateId(0x5400000000000000ULL, 1);
    auto templateRoot =
        Element(Keire::UiVisualElementType::VisualElement, Keire::AssetId(0x5400000000000000ULL, 2), "fallback-root");
    auto defaultSlot = Element(Keire::UiVisualElementType::Slot, Keire::AssetId(0x5400000000000000ULL, 3));
    defaultSlot.Children.push_back(
        Element(Keire::UiVisualElementType::Label, Keire::AssetId(0x5400000000000000ULL, 4), "default-fallback"));
    auto namedSlot = Element(Keire::UiVisualElementType::Slot, Keire::AssetId(0x5400000000000000ULL, 5), "actions");
    namedSlot.Children.push_back(
        Element(Keire::UiVisualElementType::Button, Keire::AssetId(0x5400000000000000ULL, 6), "action-fallback"));
    templateRoot.Children = {std::move(defaultSlot), std::move(namedSlot)};
    const auto templateTree = VisualTree("FallbackTemplate", std::move(templateRoot));
    auto root =
        Element(Keire::UiVisualElementType::TemplateContainer, Keire::AssetId(0x5400000000000000ULL, 7), "instance");
    root.Template = templateId;
    const auto source = VisualTree("FallbackDocument", std::move(root));
    const auto document = Keire::CreateRef<Keire::UiDocument>(
        source, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{},
        [=](const Keire::AssetId id) -> Keire::Ref<const Keire::UiVisualTreeAsset>
        { return id == templateId ? templateTree : Keire::Ref<const Keire::UiVisualTreeAsset>{}; });
    CHECK(document->Find("default-fallback").has_value());
    CHECK(document->Find("action-fallback").has_value());
}

TEST_CASE("UI template resolution rejects missing assets and rolls back the shared tree")
{
    auto root = Element(Keire::UiVisualElementType::TemplateContainer, Keire::AssetId(0x5500000000000000ULL, 1),
                        "missing-instance");
    root.Template = Keire::AssetId(0x5500000000000000ULL, 2);
    const auto source = VisualTree("MissingTemplateDocument", std::move(root));
    const auto shared = Keire::CreateRef<Keire::RuntimeUiTree>();
    const auto host = shared->Create(Keire::RuntimeUiElementType::Canvas);
    CHECK_THROWS_WITH_AS((void)Keire::CreateRef<Keire::UiDocument>(
                             source, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{}, shared, host,
                             [](Keire::AssetId) { return Keire::Ref<const Keire::UiVisualTreeAsset>{}; }),
                         doctest::Contains("could not be resolved"), std::runtime_error);
    CHECK(shared->Children(host).empty());
    CHECK(shared->Statistics().Elements == 1);
}

TEST_CASE("UI documents reject a slot declaration as the runtime root")
{
    auto root = Element(Keire::UiVisualElementType::Slot, Keire::AssetId(0x5500000000000010ULL, 1));
    const auto source = VisualTree("RootSlotDocument", std::move(root));
    CHECK_THROWS_WITH_AS((void)Keire::CreateRef<Keire::UiDocument>(source),
                         doctest::Contains("roots cannot be slot declarations"), std::invalid_argument);
}

TEST_CASE("UI template expansion rejects cycles and excessive nesting")
{
    const Keire::AssetId firstId(0x5600000000000000ULL, 1);
    const Keire::AssetId secondId(0x5600000000000000ULL, 2);
    auto firstRoot =
        Element(Keire::UiVisualElementType::TemplateContainer, Keire::AssetId(0x5600000000000010ULL, 1), "first");
    firstRoot.Template = secondId;
    auto secondRoot =
        Element(Keire::UiVisualElementType::TemplateContainer, Keire::AssetId(0x5600000000000010ULL, 2), "second");
    secondRoot.Template = firstId;
    const auto firstTree = VisualTree("FirstCycle", std::move(firstRoot));
    const auto secondTree = VisualTree("SecondCycle", std::move(secondRoot));
    auto cycleRoot =
        Element(Keire::UiVisualElementType::TemplateContainer, Keire::AssetId(0x5600000000000010ULL, 3), "cycle-root");
    cycleRoot.Template = firstId;
    const auto cycleSource = VisualTree("CycleDocument", std::move(cycleRoot));
    const std::unordered_map<Keire::AssetId, Keire::Ref<const Keire::UiVisualTreeAsset>> cycleTemplates{
        {firstId, firstTree}, {secondId, secondTree}};
    CHECK_THROWS_WITH_AS((void)Keire::CreateRef<Keire::UiDocument>(
                             cycleSource, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{},
                             [&cycleTemplates](const Keire::AssetId id) { return cycleTemplates.at(id); }),
                         doctest::Contains("contains a cycle"), std::runtime_error);

    std::unordered_map<Keire::AssetId, Keire::Ref<const Keire::UiVisualTreeAsset>> chain;
    for (std::size_t index = 0; index <= Keire::MaximumUiTemplateDepth; ++index)
    {
        const Keire::AssetId id(0x5700000000000000ULL, index + 1);
        auto chainRoot = Element(Keire::UiVisualElementType::TemplateContainer,
                                 Keire::AssetId(0x5700000000000010ULL, index + 1), "depth-" + std::to_string(index));
        chainRoot.Template = Keire::AssetId(0x5700000000000000ULL, index + 2);
        chain.emplace(id, VisualTree("DepthTemplate" + std::to_string(index), std::move(chainRoot)));
    }
    auto depthRoot = Element(Keire::UiVisualElementType::TemplateContainer, Keire::AssetId(0x5700000000000010ULL, 100),
                             "depth-root");
    depthRoot.Template = Keire::AssetId(0x5700000000000000ULL, 1);
    const auto depthSource = VisualTree("DepthDocument", std::move(depthRoot));
    CHECK_THROWS_WITH_AS((void)Keire::CreateRef<Keire::UiDocument>(
                             depthSource, std::vector<Keire::Ref<const Keire::UiStyleSheetAsset>>{},
                             [&chain](const Keire::AssetId id)
                             {
                                 const auto found = chain.find(id);
                                 return found == chain.end() ? Keire::Ref<const Keire::UiVisualTreeAsset>{}
                                                             : found->second;
                             }),
                         doctest::Contains("depth limit"), std::runtime_error);
}

TEST_CASE("UI asset validation rejects unsafe documents and incompatible panel targets")
{
    CHECK_THROWS_WITH_AS((void)Keire::UiVisualTreeAsset::ParseSource(AsBytes("<ui schemaVersion=\"2\"/>")),
                         doctest::Contains("non-empty <ui> root"), std::runtime_error);
    CHECK_THROWS_WITH_AS((void)Keire::UiStyleSheetAsset::ParseSource(AsBytes("Button { color: white; }")),
                         doctest::Contains("must begin"), std::runtime_error);

    Keire::UiPanelSettingsDefinition panel;
    panel.Target = Keire::UiPanelTarget::RenderTexture;
    CHECK_THROWS_WITH_AS(Keire::UiPanelSettingsAsset::Validate(panel), doctest::Contains("invalid target"),
                         std::invalid_argument);
    panel.RenderTexture = Id("30000000-0000-0000-0000-000000000001");
    const auto encoded = Keire::UiPanelSettingsAsset::Encode(panel);
    CHECK(Keire::UiPanelSettingsAsset::Decode(encoded)->Definition() == panel);
}

TEST_CASE("Built-in registry exposes all retained UI Toolkit asset types")
{
    const auto importers = Keire::CreateBuiltinAssetImporters();
    CHECK(std::ranges::count_if(importers, [](const auto& value) { return value.Name == "Keire.UiVisualTree"; }) == 1);
    CHECK(std::ranges::count_if(importers, [](const auto& value) { return value.Name == "Keire.UiStyleSheet"; }) == 1);
    CHECK(std::ranges::count_if(importers, [](const auto& value) { return value.Name == "Keire.UiPanelSettings"; }) ==
          1);

    const auto decoders = Keire::CreateBuiltinAssetDecoders();
    CHECK(std::ranges::count(decoders, Keire::UiVisualTreeAsset::StaticType(),
                             &Keire::AssetDecoderRegistration::Type) == 1);
    CHECK(std::ranges::count(decoders, Keire::UiStyleSheetAsset::StaticType(),
                             &Keire::AssetDecoderRegistration::Type) == 1);
    CHECK(std::ranges::count(decoders, Keire::UiPanelSettingsAsset::StaticType(),
                             &Keire::AssetDecoderRegistration::Type) == 1);
}
