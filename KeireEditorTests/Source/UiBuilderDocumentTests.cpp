#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/UiBuilderDocument.h"
#include "KeireClient/Editor/UiBuilderLiveDraft.h"
#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"
#include "KeireClient/Editor/UiStyleSourceEditor.h"
#include "KeireClient/Editor/UiStyleTokenRefactor.h"

#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/Ui/UiElements.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] Keire::UiVisualTreeDefinition TestDocument()
    {
        Keire::UiVisualTreeDefinition result;
        result.Name = "Hud";
        result.Root.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000201");
        result.Root.Name = "root";
        result.Root.Classes = {"screen"};
        Keire::UiVisualElementDefinition title;
        title.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000202");
        title.Type = Keire::UiVisualElementType::Label;
        title.Name = "title";
        title.Attributes = {{"text", "Ready"}};
        result.Root.Children.push_back(std::move(title));
        return result;
    }

    [[nodiscard]] std::span<const std::byte> AsBytes(const std::string_view value)
    {
        return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
    }
} // namespace

TEST_CASE("runtime game UI is isolated from the Scene viewport")
{
    CHECK_FALSE(KeireEditor::CompositesRuntimeGameUi(KeireEditor::EditorViewportTarget::Scene));
    CHECK(KeireEditor::CompositesRuntimeGameUi(KeireEditor::EditorViewportTarget::Game));
    CHECK_FALSE(KeireEditor::RoutesRuntimeGameUiInput(KeireEditor::EditorViewportTarget::Scene));
    CHECK(KeireEditor::RoutesRuntimeGameUiInput(KeireEditor::EditorViewportTarget::Game));
    CHECK_FALSE(KeireEditor::SubmitsRuntimeUiToSceneRenderer(Keire::RuntimeUiRenderTarget::ScreenOverlay));
    CHECK_FALSE(KeireEditor::SubmitsRuntimeUiToSceneRenderer(Keire::RuntimeUiRenderTarget::CameraOverlay));
    CHECK(KeireEditor::SubmitsRuntimeUiToSceneRenderer(Keire::RuntimeUiRenderTarget::RenderTexture));
    CHECK(KeireEditor::SubmitsRuntimeUiToSceneRenderer(Keire::RuntimeUiRenderTarget::WorldSurface));
}

TEST_CASE("UI Builder canvas gestures stay parent bounded and transform live preview geometry")
{
    const Keire::RuntimeUiRect parent{50.0F, 50.0F, 400.0F, 300.0F};
    const Keire::RuntimeUiRect initial{100.0F, 100.0F, 200.0F, 80.0F};

    const auto movedMinimum = KeireEditor::ResolveUiBuilderCanvasGesture(initial, parent, {-500.0F, -500.0F},
                                                                         KeireEditor::UiBuilderCanvasGesture::Move);
    CHECK(movedMinimum.X == doctest::Approx(50.0F));
    CHECK(movedMinimum.Y == doctest::Approx(50.0F));
    const auto movedMaximum = KeireEditor::ResolveUiBuilderCanvasGesture(initial, parent, {500.0F, 500.0F},
                                                                         KeireEditor::UiBuilderCanvasGesture::Move);
    CHECK(movedMaximum.X == doctest::Approx(250.0F));
    CHECK(movedMaximum.Y == doctest::Approx(270.0F));

    const auto resizedTopLeft = KeireEditor::ResolveUiBuilderCanvasGesture(
        initial, parent, {-500.0F, -500.0F}, KeireEditor::UiBuilderCanvasGesture::ResizeTopLeft);
    CHECK(resizedTopLeft.X == doctest::Approx(50.0F));
    CHECK(resizedTopLeft.Y == doctest::Approx(50.0F));
    CHECK(resizedTopLeft.Width == doctest::Approx(250.0F));
    CHECK(resizedTopLeft.Height == doctest::Approx(130.0F));
    const auto resizedBottomRight = KeireEditor::ResolveUiBuilderCanvasGesture(
        initial, parent, {500.0F, 500.0F}, KeireEditor::UiBuilderCanvasGesture::ResizeBottomRight);
    CHECK(resizedBottomRight.Width == doctest::Approx(350.0F));
    CHECK(resizedBottomRight.Height == doctest::Approx(250.0F));

    const auto transformed = KeireEditor::TransformUiBuilderCanvasPreviewRect({120.0F, 120.0F, 40.0F, 20.0F}, initial,
                                                                              {150.0F, 130.0F, 300.0F, 160.0F});
    CHECK(transformed.X == doctest::Approx(180.0F));
    CHECK(transformed.Y == doctest::Approx(170.0F));
    CHECK(transformed.Width == doctest::Approx(60.0F));
    CHECK(transformed.Height == doctest::Approx(40.0F));

    const Keire::RuntimeUiRect undersizedInitial{0.0F, 0.0F, 4.0F, 4.0F};
    CHECK_NOTHROW((void)KeireEditor::ResolveUiBuilderCanvasGesture(undersizedInitial, {10.0F, 10.0F, 64.0F, 64.0F},
                                                                   {-20.0F, -20.0F},
                                                                   KeireEditor::UiBuilderCanvasGesture::ResizeTopLeft));
}

TEST_CASE("UI Builder canvas hit testing preserves a move target inside compact controls")
{
    const Keire::UiItemRect label{{100.0F, 100.0F}, {168.0F, 114.0F}};
    CHECK(KeireEditor::HitTestUiBuilderCanvasGesture(label, {134.0F, 107.0F}) ==
          KeireEditor::UiBuilderCanvasGesture::Move);
    CHECK(KeireEditor::HitTestUiBuilderCanvasGesture(label, {134.0F, 100.0F}) ==
          KeireEditor::UiBuilderCanvasGesture::ResizeTop);
    CHECK(KeireEditor::HitTestUiBuilderCanvasGesture(label, {168.0F, 107.0F}) ==
          KeireEditor::UiBuilderCanvasGesture::ResizeRight);
    CHECK(KeireEditor::HitTestUiBuilderCanvasGesture(label, {100.0F, 100.0F}) ==
          KeireEditor::UiBuilderCanvasGesture::ResizeTopLeft);
    CHECK(KeireEditor::HitTestUiBuilderCanvasGesture(label, {80.0F, 80.0F}) ==
          KeireEditor::UiBuilderCanvasGesture::None);

    const Keire::UiItemRect compact{{20.0F, 20.0F}, {28.0F, 28.0F}};
    CHECK(KeireEditor::HitTestUiBuilderCanvasGesture(compact, {24.0F, 24.0F}) ==
          KeireEditor::UiBuilderCanvasGesture::Move);
}

TEST_CASE("UI Builder empty canvas placement is visible and exact geometry survives relayout")
{
    KeireEditor::UiBuilderPreviewSettings defaults;
    CHECK(defaults.Width == 1920);
    CHECK(defaults.Height == 1080);
    CHECK(defaults.ReferenceWidth == 1920);
    CHECK(defaults.ReferenceHeight == 1080);

    const Keire::RuntimeUiRect compactRoot{0.0F, 0.0F, 64.0F, 64.0F};
    const auto compactPlacement = KeireEditor::ResolveUiBuilderCanvasPlacement(
        compactRoot, KeireEditor::UiBuilderCanvasControlDefaultSize(Keire::UiVisualElementType::Label), {32.0F, 32.0F});
    CHECK(compactPlacement.X == doctest::Approx(8.0F));
    CHECK(compactPlacement.Y == doctest::Approx(12.0F));
    CHECK(compactPlacement.Width == doctest::Approx(48.0F));
    CHECK(compactPlacement.Height == doctest::Approx(40.0F));

    Keire::UiVisualTreeDefinition definition;
    definition.Name = "EmptyCanvas";
    definition.Root.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000231");
    definition.Root.Name = "root";
    const auto temporary = std::filesystem::temp_directory_path() / "Keire-UiBuilder-Canvas-Placement.keireui";
    std::error_code error;
    std::filesystem::remove(temporary, error);

    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000232"), definition, 1, temporary, {});
    const auto label = document.AddCanvasElement(definition.Root.StableId, Keire::UiVisualElementType::Label,
                                                 compactRoot, {32.0F, 32.0F});
    CHECK(label);
    CHECK(document.Definition().Root.Children.size() == 1);

    auto candidate = document.Definition();
    const Keire::RuntimeUiRect transformed{13.125F, 19.75F, 37.5F, 28.25F};
    KeireEditor::PersistUiBuilderCanvasGeometry(candidate.Root.Children.front(), compactRoot, transformed);
    CHECK(document.Edit("Transform UI element exactly", std::move(candidate)));
    document.Save();
    document.ReloadFromSource();

    KeireEditor::UiBuilderPreviewSettings settings;
    settings.Width = 64;
    settings.Height = 64;
    settings.ReferenceWidth = 64;
    settings.ReferenceHeight = 64;
    settings.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    const auto first = KeireEditor::BuildUiBuilderRetainedPreview(document.Definition(), label, settings);
    const auto second = KeireEditor::BuildUiBuilderRetainedPreview(document.Definition(), label, settings);
    REQUIRE(first.SelectedState);
    REQUIRE(second.SelectedState);
    CHECK(first.SelectedState->Rect.X == doctest::Approx(transformed.X));
    CHECK(first.SelectedState->Rect.Y == doctest::Approx(transformed.Y));
    CHECK(first.SelectedState->Rect.Width == doctest::Approx(transformed.Width));
    CHECK(first.SelectedState->Rect.Height == doctest::Approx(transformed.Height));
    CHECK(second.SelectedState->Rect.X == doctest::Approx(first.SelectedState->Rect.X));
    CHECK(second.SelectedState->Rect.Y == doctest::Approx(first.SelectedState->Rect.Y));
    CHECK(second.SelectedState->Rect.Width == doctest::Approx(first.SelectedState->Rect.Width));
    CHECK(second.SelectedState->Rect.Height == doctest::Approx(first.SelectedState->Rect.Height));

    std::filesystem::remove(temporary, error);
}

TEST_CASE("UI Builder debugger exposes resolved runtime layout and profiler state")
{
    Keire::UiVisualTreeDefinition definition;
    definition.Name = "Debugger";
    definition.Root.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000221");
    definition.Root.Name = "root";
    definition.Root.InlineStyles = {{"width", "1280"}, {"height", "720"}, {"padding", "8"}};
    Keire::UiVisualElementDefinition button;
    button.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000222");
    button.Type = Keire::UiVisualElementType::Button;
    button.Name = "start";
    button.Attributes = {{"text", "Start"}};
    button.InlineStyles = {{"width", "320"}, {"height", "48"}, {"opacity", "0.75"}};
    definition.Root.Children.push_back(button);

    const auto snapshot =
        KeireEditor::BuildUiBuilderRuntimeDebugSnapshot(definition, button.StableId, {}, 1280.0F, 720.0F);
    CHECK(snapshot.Statistics.Elements == 2);
    CHECK(snapshot.Statistics.VisibleElements == 2);
    CHECK(snapshot.Statistics.LayoutPasses == 1);
    CHECK(snapshot.InlineStyleProperties == 6);
    CHECK(snapshot.LinkedStyleSheets == 0);
    CHECK(snapshot.ResolvedStyleSheets == 0);
    REQUIRE(snapshot.SelectedState);
    CHECK(snapshot.SelectedState->Style.Width == doctest::Approx(320.0F));
    CHECK(snapshot.SelectedState->Style.Height == doctest::Approx(48.0F));
    CHECK(snapshot.SelectedState->Style.Opacity == doctest::Approx(0.75F));
    // VisualElement defaults to a column flex container with stretch alignment, matching Unity UI Toolkit.
    CHECK(snapshot.SelectedState->Rect.Width == doctest::Approx((1280.0F - 16.0F) * (1280.0F / 1920.0F)));
    CHECK_THROWS_AS(
        (void)KeireEditor::BuildUiBuilderRuntimeDebugSnapshot(definition, button.StableId, {}, 0.0F, 720.0F),
        std::invalid_argument);
}

TEST_CASE("UI Builder edits preserve stable hierarchy identity and support undo")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-UiBuilderDocument-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    const auto source = root / "Hud.keireui";
    const auto original = TestDocument();
    const auto sourceBytes = Keire::UiVisualTreeAsset::EncodeSource(original);
    Keire::Detail::WriteTextFileAtomically(source,
                                           {reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size()});

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "UI Builder"});
    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000210"), original, 1, source, undo);
    document.Select(original.Root.Children.front().StableId);
    const auto button = document.AddElement(document.Selection(), Keire::UiVisualElementType::Button);
    REQUIRE(button);
    CHECK(document.ParentOf(button) == original.Root.Children.front().StableId);
    CHECK(document.Dirty());
    REQUIRE(undo->CanUndo());
    CHECK(document.Undo());
    CHECK(document.Find(button) == nullptr);
    CHECK_FALSE(document.Dirty());
    REQUIRE(undo->CanRedo());
    CHECK(document.Redo());
    CHECK(document.Find(button) != nullptr);
    CHECK(document.Dirty());

    CHECK_FALSE(document.ReparentElement(original.Root.Children.front().StableId, button, 0));
    CHECK(document.ParentOf(original.Root.Children.front().StableId) == original.Root.StableId);

    document.Save();
    CHECK_FALSE(document.Dirty());
    const auto saved = Keire::Detail::ReadTextFile(source, Keire::MaximumUiDocumentBytes);
    const auto decoded = Keire::UiVisualTreeAsset::ParseSource(std::as_bytes(std::span(saved)));
    CHECK(decoded.Name == "Hud");
    CHECK(decoded.Root.StableId == original.Root.StableId);
    CHECK(decoded.Root.Children.front().Children.front().StableId == button);

    undoService->Close();
    std::filesystem::remove_all(root, error);
}

TEST_CASE("UI Builder source edits are transactional on parse failure")
{
    const auto definition = TestDocument();
    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000211"), definition, 1, "Unused.keireui");
    const auto before = Keire::UiVisualTreeAsset::Encode(document.Definition());
    const std::string malformed = "<ui schemaVersion=\"1\"><Label></ui>";
    std::string diagnostic;
    CHECK_FALSE(document.ApplySource(std::as_bytes(std::span(malformed)), diagnostic));
    CHECK_FALSE(diagnostic.empty());
    CHECK(Keire::UiVisualTreeAsset::Encode(document.Definition()) == before);
    CHECK_FALSE(document.Dirty());
}

TEST_CASE("UI Builder library controls receive visible finite authoring defaults")
{
    auto definition = TestDocument();
    definition.Root.Children.clear();
    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000219"), definition, 1, "Unused.keireui");

    constexpr std::array types{Keire::UiVisualElementType::VisualElement, Keire::UiVisualElementType::Label,
                               Keire::UiVisualElementType::Image,         Keire::UiVisualElementType::Button,
                               Keire::UiVisualElementType::TextField,     Keire::UiVisualElementType::Toggle,
                               Keire::UiVisualElementType::Slider,        Keire::UiVisualElementType::ProgressBar,
                               Keire::UiVisualElementType::ScrollView,    Keire::UiVisualElementType::ListView,
                               Keire::UiVisualElementType::TreeView,      Keire::UiVisualElementType::DropdownField,
                               Keire::UiVisualElementType::Foldout,       Keire::UiVisualElementType::TabView,
                               Keire::UiVisualElementType::Toolbar,       Keire::UiVisualElementType::Spacer};
    std::vector<Keire::AssetId> added;
    for (const auto type : types)
        added.push_back(document.AddElement(document.Definition().Root.StableId, type));

    KeireEditor::UiBuilderPreviewSettings settings;
    settings.Width = 1920;
    settings.Height = 1080;
    const auto preview = KeireEditor::BuildUiBuilderRetainedPreview(document.Definition(),
                                                                    document.Definition().Root.StableId, settings, {});
    for (const auto id : added)
    {
        const auto found = std::ranges::find(preview.Elements, id, &KeireEditor::UiBuilderPreviewElement::StableId);
        REQUIRE(found != preview.Elements.end());
        CHECK(std::isfinite(found->State.Rect.X));
        CHECK(std::isfinite(found->State.Rect.Y));
        CHECK(found->State.Rect.Width > 0.0F);
        CHECK(found->State.Rect.Height > 0.0F);
    }
}

TEST_CASE("UI Builder preview state clamps devices and keeps navigation transient")
{
    KeireEditor::UiBuilderPreviewSettings settings;
    settings.ApplyPreset(KeireEditor::UiBuilderResolutionPreset::Hd);
    CHECK(settings.Width == 1280);
    CHECK(settings.Height == 720);
    settings.ApplyOrientation(KeireEditor::UiBuilderOrientation::Portrait);
    CHECK(settings.Width == 720);
    CHECK(settings.Height == 1280);
    CHECK(settings.Preset == KeireEditor::UiBuilderResolutionPreset::Custom);
    settings.ApplyOrientation(KeireEditor::UiBuilderOrientation::Landscape);
    CHECK(settings.Width == 1280);
    CHECK(settings.Height == 720);
    settings.Width = 1;
    settings.Height = 20'000;
    settings.ReferenceWidth = 8;
    settings.ReferenceHeight = 20'000;
    settings.Dpi = std::numeric_limits<float>::infinity();
    settings.UserScale = 8.0F;
    settings.VerticalGuide = std::numeric_limits<float>::infinity();
    settings.HorizontalGuide = 20'000.0F;
    settings.SafeArea = {100.0F, 40.0F, 100.0F, 40.0F};
    settings.Preset = KeireEditor::UiBuilderResolutionPreset::Custom;
    settings.Normalize();
    CHECK(settings.Width == 64);
    CHECK(settings.Height == 8192);
    CHECK(settings.ReferenceWidth == 64);
    CHECK(settings.ReferenceHeight == 8192);
    CHECK(settings.Dpi == doctest::Approx(96.0F));
    CHECK(settings.UserScale == doctest::Approx(2.0F));
    CHECK(settings.VerticalGuide == doctest::Approx(-1.0F));
    CHECK(settings.HorizontalGuide == doctest::Approx(8192.0F));
    CHECK(settings.SafeArea.Left == doctest::Approx(64.0F));
    CHECK(settings.SafeArea.Right == doctest::Approx(0.0F));

    settings.SetPseudoState(Keire::UiStylePseudoState::Hover, true);
    settings.SetPseudoState(Keire::UiStylePseudoState::Focus, true);
    CHECK(settings.HasPseudoState(Keire::UiStylePseudoState::Hover));
    CHECK(settings.HasPseudoState(Keire::UiStylePseudoState::Focus));
    settings.SetPseudoState(Keire::UiStylePseudoState::Hover, false);
    CHECK_FALSE(settings.HasPseudoState(Keire::UiStylePseudoState::Hover));

    settings.PanBy({25.0F, -15.0F});
    settings.ZoomBy(2.0F);
    CHECK(settings.Pan == Keire::Vector2{25.0F, -15.0F});
    CHECK(settings.Zoom == doctest::Approx(2.0F));
    settings.ResetView();
    CHECK(settings.Pan == Keire::Vector2{});
    CHECK(settings.Zoom == doctest::Approx(1.0F));
    CHECK(settings.CanvasSettings().AccessibilityScale == doctest::Approx(2.0F));

    CHECK_FALSE(settings.MatchGameView(0, 720));
    CHECK(settings.MatchGameView(1600, 900));
    CHECK(settings.Width == 1600);
    CHECK(settings.Height == 900);
    CHECK(settings.Preset == KeireEditor::UiBuilderResolutionPreset::Custom);
}

TEST_CASE("UI Builder retained preview applies safe area and selected pseudo-state without authoring undo")
{
    auto definition = TestDocument();
    const auto styleId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000220");
    definition.StyleSheets = {styleId};
    constexpr std::string_view styleSource = R"css(@keire-style 1;
#title {
  width: 240px;
  height: 64px;
  background-color: #112233ff;
}
#title:hover {
  background-color: #ff3300ff;
}
)css";
    const auto style =
        Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(AsBytes(styleSource)));

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "UI Builder Preview"});
    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000221"), definition, 1, "Preview.keireui",
                  undo);
    const auto title = definition.Root.Children.front().StableId;
    document.Select(title);

    KeireEditor::UiBuilderPreviewSettings settings;
    settings.Width = 800;
    settings.Height = 450;
    settings.Preset = KeireEditor::UiBuilderResolutionPreset::Custom;
    settings.SafeArea = {24.0F, 12.0F, 16.0F, 8.0F};
    settings.SetPseudoState(Keire::UiStylePseudoState::Hover, true);
    const std::array<Keire::Ref<const Keire::UiStyleSheetAsset>, 1> styles{style};
    const auto preview = KeireEditor::BuildUiBuilderRetainedPreview(definition, title, settings, styles);

    REQUIRE(preview.SelectedState);
    CHECK(preview.SelectedState->Rect.X >= doctest::Approx(24.0F));
    CHECK(preview.SelectedState->Rect.Y >= doctest::Approx(12.0F));
    CHECK(preview.SelectedState->Style.Background.Red == doctest::Approx(1.0F));
    CHECK(preview.SelectedState->Style.Background.Green == doctest::Approx(0.2F));
    CHECK_FALSE(preview.DrawCommands.empty());
    CHECK(preview.Elements.size() == 2);
    CHECK(preview.LinkedStyleSheets == 1);
    CHECK(preview.ResolvedStyleSheets == 1);
    CHECK_FALSE(document.Dirty());
    CHECK_FALSE(undo->CanUndo());

    undoService->Close();
}

TEST_CASE("UI Builder preview skips zero-area draw commands from data-bound custom controls")
{
    constexpr std::string_view source = R"xml(<?xml version="1.0" encoding="utf-8"?>
<ui schemaVersion="1" name="Data Binding and Custom Controls">
  <VisualElement id="e40b1201-1111-4000-8000-000000000001" name="settings" class="card">
    <Label id="e40b1201-1111-4000-8000-000000000002" name="player-name" class="title" bind:text="Player.DisplayName"/>
    <TextField id="e40b1201-1111-4000-8000-000000000003" name="display-name" bind-two-way:value="Player.DisplayName"/>
    <Toggle id="e40b1201-1111-4000-8000-000000000004" name="show-profiler" text="Show profiler" bind-two-way:checked="Settings.ShowProfiler"/>
    <Slider id="e40b1201-1111-4000-8000-000000000005" name="master-volume" minimum="0" maximum="1" bind-two-way:value="Settings.MasterVolume"/>
    <FeatureGauge id="e40b1201-1111-4000-8000-000000000006" name="frame-rate" label="Frame rate" bind:value="Telemetry.FrameRate"/>
    <ListView id="e40b1201-1111-4000-8000-000000000007" name="recent-events" bind:items-source="Telemetry.RecentEvents"/>
  </VisualElement>
</ui>)xml";
    constexpr std::string_view styleSource = R"css(@keire-style 1;
.card {
  width: 560px;
  padding: 24px;
  gap: 12px;
  flex-direction: column;
  background-color: #101827e8;
}
.title {
  height: 42px;
  color: #f3f8ffff;
  font-size: 30px;
}
)css";
    auto definition = Keire::UiVisualTreeAsset::ParseSource(AsBytes(source));
    const auto style =
        Keire::CreateRef<Keire::UiStyleSheetAsset>(Keire::UiStyleSheetAsset::ParseSource(AsBytes(styleSource)));
    const std::array<Keire::Ref<const Keire::UiStyleSheetAsset>, 1> styles{style};
    KeireEditor::UiBuilderPreviewSettings settings;
    settings.Width = 1920;
    settings.Height = 1080;

    const auto preview =
        KeireEditor::BuildUiBuilderRetainedPreview(definition, definition.Root.StableId, settings, styles);

    REQUIRE_FALSE(preview.DrawCommands.empty());
    for (const auto& command : preview.DrawCommands)
    {
        if (command.Type == Keire::RuntimeUiDrawType::PushClip || command.Type == Keire::RuntimeUiDrawType::PopClip)
        {
            continue;
        }
        const auto clipped = command.Rect.Intersect(command.ClipRect);
        CHECK_FALSE(clipped.Empty());
        CHECK(std::isfinite(clipped.X));
        CHECK(std::isfinite(clipped.Y));
        CHECK(std::isfinite(clipped.Width));
        CHECK(std::isfinite(clipped.Height));
    }
}

TEST_CASE("UI Builder retained preview resolves referenced templates")
{
    const auto templateId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000240");
    Keire::UiVisualTreeDefinition templateDefinition;
    templateDefinition.Name = "Card";
    templateDefinition.Root.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000241");
    templateDefinition.Root.Type = Keire::UiVisualElementType::VisualElement;
    templateDefinition.Root.Name = "card";
    Keire::UiVisualElementDefinition label;
    label.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000242");
    label.Type = Keire::UiVisualElementType::Label;
    label.Name = "caption";
    label.Attributes = {{"text", "Resolved template"}};
    label.InlineStyles = {{"width", "240"}, {"height", "40"}};
    templateDefinition.Root.Children.push_back(label);
    const auto templateAsset = Keire::CreateRef<Keire::UiVisualTreeAsset>(templateDefinition);

    Keire::UiVisualTreeDefinition document;
    document.Name = "Template Host";
    document.Root.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000243");
    document.Root.Type = Keire::UiVisualElementType::TemplateContainer;
    document.Root.Name = "host";
    document.Root.Template = templateId;
    document.Root.InlineStyles = {{"width", "320"}, {"height", "180"}};

    KeireEditor::UiBuilderPreviewSettings settings;
    settings.Width = 320;
    settings.Height = 180;
    settings.Preset = KeireEditor::UiBuilderResolutionPreset::Custom;
    const auto preview = KeireEditor::BuildUiBuilderRetainedPreview(
        document, document.Root.StableId, settings, {},
        [=](const Keire::AssetId requested) -> Keire::Ref<const Keire::UiVisualTreeAsset>
        { return requested == templateId ? templateAsset : Keire::Ref<const Keire::UiVisualTreeAsset>{}; });

    CHECK(preview.Statistics.Elements == 3);
    CHECK(preview.SelectedState.has_value());
    CHECK_FALSE(preview.DrawCommands.empty());
}

TEST_CASE("UI Builder multi-selection clipboard and reparenting are atomic and regenerate identity")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-UiBuilderClipboard-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    const auto source = root / "Clipboard.keireui";
    auto definition = TestDocument();
    Keire::UiVisualElementDefinition button;
    button.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000230");
    button.Type = Keire::UiVisualElementType::Button;
    button.Name = "action";
    Keire::UiVisualElementDefinition icon;
    icon.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000233");
    icon.Type = Keire::UiVisualElementType::Image;
    icon.Name = "action-icon";
    button.Children.push_back(icon);
    Keire::UiVisualElementDefinition panel;
    panel.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000231");
    panel.Name = "panel";
    definition.Root.Children.push_back(button);
    definition.Root.Children.push_back(panel);
    const auto sourceBytes = Keire::UiVisualTreeAsset::EncodeSource(definition);
    Keire::Detail::WriteTextFileAtomically(source,
                                           {reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size()});

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "UI Builder Clipboard"});
    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000232"), definition, 1, source, undo);
    const std::array selection{button.StableId, definition.Root.Children[0].StableId};
    document.SetSelection(selection, definition.Root.Children[0].StableId);
    CHECK(document.Selections().size() == 2);
    CHECK(document.IsSelected(button.StableId));

    const auto clipboard = document.CopySelection();
    REQUIRE(clipboard.Elements.size() == 2);
    const auto pasted = document.PasteElements(definition.Root.StableId, clipboard);
    REQUIRE(pasted.size() == 2);
    CHECK(pasted[0] != selection[0]);
    CHECK(pasted[1] != selection[1]);
    REQUIRE(document.Find(pasted[0]));
    REQUIRE(document.Find(pasted[1]));
    CHECK(document.Find(pasted[0])->Name == "title-copy");
    CHECK(document.Find(pasted[1])->Name == "action-copy");
    REQUIRE(document.Find(pasted[1])->Children.size() == 1);
    CHECK(document.Find(pasted[1])->Children.front().StableId != icon.StableId);
    CHECK(document.Find(pasted[1])->Children.front().Name == "action-icon-copy");
    CHECK(undo->CanUndo());
    CHECK(document.Undo());
    CHECK(document.Find(pasted[0]) == nullptr);
    CHECK(document.Find(pasted[1]) == nullptr);
    CHECK(document.Redo());
    REQUIRE(document.Find(pasted[0]));
    REQUIRE(document.Find(pasted[1]));

    CHECK(document.ReparentElements(pasted, panel.StableId, 0));
    CHECK(document.ParentOf(pasted[0]) == panel.StableId);
    CHECK(document.ParentOf(pasted[1]) == panel.StableId);
    CHECK(document.Undo());
    CHECK(document.ParentOf(pasted[0]) == definition.Root.StableId);
    CHECK(document.ParentOf(pasted[1]) == definition.Root.StableId);
    CHECK(document.Redo());
    CHECK(document.ParentOf(pasted[0]) == panel.StableId);

    document.SetSelection(pasted, pasted.back());
    CHECK(document.RemoveSelection());
    CHECK(document.Find(pasted[0]) == nullptr);
    CHECK(document.Find(pasted[1]) == nullptr);
    CHECK(document.Undo());
    REQUIRE(document.Find(pasted[0]));
    REQUIRE(document.Find(pasted[1]));

    document.Save();
    CHECK_FALSE(document.Dirty());
    document.ReloadFromSource();
    CHECK(document.ParentOf(pasted[0]) == panel.StableId);
    CHECK(document.ParentOf(pasted[1]) == panel.StableId);
    CHECK_FALSE(undo->CanUndo());

    undoService->Close();
    std::filesystem::remove_all(root, error);
}

TEST_CASE("UI Builder classes templates slots and binding modes persist through save and reload")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-UiBuilderBindings-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    const auto source = root / "Bindings.keireui";
    auto definition = TestDocument();
    Keire::UiVisualElementDefinition button;
    button.StableId = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000240");
    button.Type = Keire::UiVisualElementType::Button;
    button.Name = "action";
    definition.Root.Children.push_back(button);
    const auto sourceBytes = Keire::UiVisualTreeAsset::EncodeSource(definition);
    Keire::Detail::WriteTextFileAtomically(source,
                                           {reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size()});

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "UI Builder Bindings"});
    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000241"), definition, 1, source, undo);
    const auto title = definition.Root.Children[0].StableId;
    const auto templateAsset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000242");
    const auto templateElement = document.AddTemplate(definition.Root.StableId, templateAsset);
    const auto slotElement = document.AddSlot(definition.Root.StableId, "content");
    CHECK(document.Find(templateElement)->Template == templateAsset);
    CHECK(document.Find(slotElement)->Type == Keire::UiVisualElementType::Slot);
    CHECK(document.Find(slotElement)->Name == "content");
    CHECK(document.Find(slotElement)->Slot.empty());
    CHECK(document.SetSlot(button.StableId, "content"));

    const std::array classTargets{title, button.StableId};
    CHECK(document.SetClasses(classTargets, {"shared", "interactive", "shared"}));
    CHECK(document.Find(title)->Classes == std::vector<std::string>{"shared", "interactive"});
    CHECK(document.Find(button.StableId)->Classes == std::vector<std::string>{"shared", "interactive"});
    const std::vector<Keire::UiBindingDefinition> bindings{
        {"text", "player.displayName", "OneWay"},
        {"value", "settings.volume", "TwoWay"},
        {"enabled", "session.ready", "OneTime"},
    };
    CHECK(document.SetBindings(title, bindings));
    const auto beforeInvalid = Keire::UiVisualTreeAsset::Encode(document.Definition());
    CHECK_THROWS((void)document.SetBindings(title, {{"text", "one", "OneWay"}, {"text", "two", "OneWay"}}));
    CHECK(Keire::UiVisualTreeAsset::Encode(document.Definition()) == beforeInvalid);

    document.Save();
    const auto authored = Keire::Detail::ReadTextFile(source, Keire::MaximumUiDocumentBytes);
    CHECK(authored.find("bind:text=\"player.displayName\"") != std::string::npos);
    CHECK(authored.find("bind-two-way:value=\"settings.volume\"") != std::string::npos);
    CHECK(authored.find("bind-one-time:enabled=\"session.ready\"") != std::string::npos);
    document.ReloadFromSource();
    REQUIRE(document.Find(title));
    CHECK(document.Find(title)->Bindings == bindings);
    CHECK(document.Find(button.StableId)->Slot == "content");
    CHECK(document.Find(templateElement)->Template == templateAsset);
    CHECK(document.Find(slotElement)->Name == "content");

    undoService->Close();
    std::filesystem::remove_all(root, error);
}

TEST_CASE("UI Builder live debugger preserves immutable snapshots across failed Play reloads and repeated shutdown")
{
    const auto visualTree = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000250");
    const auto selected = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000251");
    auto first = std::make_shared<KeireEditor::UiBuilderLiveDebugSnapshot>();
    first->VisualTree = visualTree;
    first->Sequence = 7;
    first->VertexCount = 144;
    first->AtlasTextureCount = 12;
    first->AtlasBytes = 4096;
    first->StyleMilliseconds = 0.15F;
    first->LayoutMilliseconds = 0.25F;
    first->RepaintMilliseconds = 0.35F;
    first->DirtyReasons = {selected.ToString() + ": style, descendant"};
    first->SelectorPrecedence = {selected.ToString() + "  Button.primary  specificity 11  source 2"};
    first->EventPropagationTraceAvailable = true;
    first->DirtyReasonsAvailable = true;
    first->SelectorPrecedenceAvailable = true;
    KeireEditor::UiBuilderLiveDebugDocument document;
    document.DocumentGeneration = 3;
    document.PresentationStatistics.Elements = 2;
    document.FocusedElement = selected;
    document.Elements.push_back({selected, {.Focused = true}});
    document.EventTrace.push_back({.Type = Keire::RuntimeUiEventType::Click,
                                   .PropagationPhase = KeireEditor::UiBuilderLiveDebugEvent::Phase::Bubble,
                                   .Sequence = 42,
                                   .Target = selected,
                                   .CurrentTarget = selected});
    first->Documents.push_back(document);

    KeireEditor::UiBuilderLiveDebugStore store;
    store.Refresh(visualTree, {first, {}});
    CHECK(store.Status() == KeireEditor::UiBuilderLiveDebugStatus::Live);
    REQUIRE(store.Current());
    CHECK(store.Current()->Sequence == 7);
    CHECK(store.Current()->Documents.front().FocusedElement == selected);
    CHECK(store.Current()->VertexCount == 144);
    CHECK(store.Current()->AtlasTextureCount == 12);
    CHECK(store.Current()->AtlasBytes == 4096);
    CHECK(store.Current()->StyleMilliseconds == doctest::Approx(0.15F));
    CHECK(store.Current()->LayoutMilliseconds == doctest::Approx(0.25F));
    CHECK(store.Current()->RepaintMilliseconds == doctest::Approx(0.35F));
    CHECK(store.Current()->DirtyReasonsAvailable);
    CHECK(store.Current()->SelectorPrecedenceAvailable);
    CHECK(store.Current()->EventPropagationTraceAvailable);
    CHECK(store.Current()->Documents.front().EventTrace.front().Sequence == 42);
    CHECK(store.Current()->Documents.front().EventTrace.front().PropagationPhase ==
          KeireEditor::UiBuilderLiveDebugEvent::Phase::Bubble);

    std::weak_ptr<const KeireEditor::UiBuilderLiveDebugSnapshot> lifetime = first;
    first.reset();
    store.Refresh(visualTree, {{}, "Play reload failed before the candidate presentation committed."});
    CHECK(store.Status() == KeireEditor::UiBuilderLiveDebugStatus::Stale);
    REQUIRE(store.Current());
    CHECK(store.Current()->Sequence == 7);
    CHECK(store.Diagnostic().find("reload failed") != std::string::npos);
    CHECK_FALSE(lifetime.expired());

    auto outOfOrder = std::make_shared<KeireEditor::UiBuilderLiveDebugSnapshot>();
    outOfOrder->VisualTree = visualTree;
    outOfOrder->Sequence = 6;
    store.Refresh(visualTree, {outOfOrder, {}});
    CHECK(store.Status() == KeireEditor::UiBuilderLiveDebugStatus::Stale);
    CHECK(store.Current()->Sequence == 7);

    const auto retained = store.Current();
    store.Close();
    store.Close();
    CHECK(store.Status() == KeireEditor::UiBuilderLiveDebugStatus::Unavailable);
    CHECK_FALSE(store.Current());
    CHECK(store.Diagnostic().empty());
    CHECK(retained->Documents.front().Elements.front().StableId == selected);
}

TEST_CASE("UI Builder live drafts replace matching Play documents and revert without persisting")
{
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(specification));
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000260");
    const auto original = TestDocument();
    REQUIRE(assets->PublishDevelopmentAsset(asset, Keire::CreateRef<Keire::UiVisualTreeAsset>(original)));

    auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("UI"));
    auto entity = scene->CreateEntity("HUD");
    const auto component = entity.AddComponent<Keire::UiDocumentComponent>();
    REQUIRE(component);
    component->SetVisualTree(asset);
    auto presentation = Keire::CreateRef<Keire::ScenePresentationRuntime>(assets, Keire::Ref<Keire::AudioSystem>{});
    presentation->Synchronize(scene, 1280.0F, 720.0F, true);
    const auto originalTitle = presentation->FindUiDocumentElement(entity.Id(), "title");
    REQUIRE(originalTitle);
    CHECK(presentation->ReadUiDocumentElementText(entity.Id(), originalTitle->DocumentGeneration,
                                                  originalTitle->Element) == "Ready");

    auto draft = original;
    draft.Root.Children.front().Attributes = {{"text", "Unsaved Play draft"}};
    KeireEditor::UiBuilderLiveDraftSession liveDraft;
    liveDraft.Synchronize(assets, true, asset, 2, true, draft);
    REQUIRE(liveDraft.Active());
    CHECK(liveDraft.DocumentGeneration() == 2);
    presentation->Synchronize(scene, 1280.0F, 720.0F, true);
    const auto draftTitle = presentation->FindUiDocumentElement(entity.Id(), "title");
    REQUIRE(draftTitle);
    CHECK(draftTitle->DocumentGeneration != originalTitle->DocumentGeneration);
    CHECK_FALSE(
        presentation->UiDocumentElementAlive(entity.Id(), originalTitle->DocumentGeneration, originalTitle->Element));
    CHECK(presentation->ReadUiDocumentElementText(entity.Id(), draftTitle->DocumentGeneration, draftTitle->Element) ==
          "Unsaved Play draft");

    liveDraft.Close();
    liveDraft.Close();
    presentation->Synchronize(scene, 1280.0F, 720.0F, true);
    const auto revertedTitle = presentation->FindUiDocumentElement(entity.Id(), "title");
    REQUIRE(revertedTitle);
    CHECK(presentation->ReadUiDocumentElementText(entity.Id(), revertedTitle->DocumentGeneration,
                                                  revertedTitle->Element) == "Ready");

    liveDraft.Synchronize(assets, true, asset, 3, true, draft);
    REQUIRE(liveDraft.Active());
    liveDraft.Commit(assets, asset, draft);
    CHECK_FALSE(liveDraft.Active());
    liveDraft.Close();
    presentation->Synchronize(scene, 1280.0F, 720.0F, true);
    const auto committedTitle = presentation->FindUiDocumentElement(entity.Id(), "title");
    REQUIRE(committedTitle);
    CHECK(presentation->ReadUiDocumentElementText(entity.Id(), committedTitle->DocumentGeneration,
                                                  committedTitle->Element) == "Unsaved Play draft");
    presentation->Clear();
    assets->Close();
}

TEST_CASE("UI Builder live style drafts retain the last valid preview and restore the imported baseline")
{
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.Decoders.push_back(Keire::CreateUiStyleSheetAssetDecoder());
    auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(specification));
    const auto asset = Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000263");
    const auto original =
        Keire::UiStyleSheetAsset::ParseSource(AsBytes("@keire-style 1;\nLabel { color: #ffffffff; }\n"));
    REQUIRE(assets->PublishDevelopmentAsset(asset, Keire::CreateRef<Keire::UiStyleSheetAsset>(original)));

    auto draft = original;
    draft.Rules.front().Properties.front().Value = "#33aaffff";
    KeireEditor::UiBuilderLiveDraftSession liveDraft;
    liveDraft.SynchronizeStyle(assets, asset, 2, true, true, draft);
    REQUIRE(liveDraft.StyleActive());
    REQUIRE(assets->Load<Keire::UiStyleSheetAsset>(asset).TryGetLoaded());
    CHECK(assets->Load<Keire::UiStyleSheetAsset>(asset).TryGetLoaded()->Definition() == draft);

    auto rejected = draft;
    rejected.Rules.front().Properties.front().Value = "not-a-color";
    liveDraft.SynchronizeStyle(assets, asset, 3, true, false, rejected);
    REQUIRE(liveDraft.StyleActive());
    CHECK(assets->Load<Keire::UiStyleSheetAsset>(asset).TryGetLoaded()->Definition() == draft);

    liveDraft.CloseStyle();
    liveDraft.CloseStyle();
    CHECK_FALSE(liveDraft.StyleActive());
    CHECK(assets->Load<Keire::UiStyleSheetAsset>(asset).TryGetLoaded()->Definition() == original);

    liveDraft.SynchronizeStyle(assets, asset, 4, true, true, draft);
    REQUIRE(liveDraft.StyleActive());
    liveDraft.CommitStyle(assets, asset, draft);
    CHECK_FALSE(liveDraft.StyleActive());
    liveDraft.CloseStyle();
    CHECK(assets->Load<Keire::UiStyleSheetAsset>(asset).TryGetLoaded()->Definition() == draft);
    assets->Close();
}

TEST_CASE("UI Toolkit authoring assets publish immediately without waiting for an import catalog")
{
    Keire::AssetSystemSpecification specification;
    specification.Mode = Keire::AssetMode::Development;
    specification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    specification.Decoders.push_back(Keire::CreateUiStyleSheetAssetDecoder());
    specification.Decoders.push_back(Keire::CreateUiPanelSettingsAssetDecoder());
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(specification));
    const auto visualTree = Keire::AssetId::Generate();
    const auto styleSheet = Keire::AssetId::Generate();
    const auto panelSettings = Keire::AssetId::Generate();
    const auto visualTreeSource = Keire::UiVisualTreeAsset::EncodeSource(TestDocument());
    constexpr std::string_view styleSheetSource = "@keire-style 1;\nLabel { color: #ffffffff; }\n";
    const auto panelSettingsSource = Keire::UiPanelSettingsAsset::Encode({});
    std::string diagnostic;

    REQUIRE(KeireEditor::PublishUiToolkitAuthoringAsset(assets, visualTree, Keire::UiVisualTreeAsset::StaticType(),
                                                        visualTreeSource, diagnostic));
    CHECK(diagnostic.empty());
    REQUIRE(KeireEditor::PublishUiToolkitAuthoringAsset(assets, styleSheet, Keire::UiStyleSheetAsset::StaticType(),
                                                        AsBytes(styleSheetSource), diagnostic));
    CHECK(diagnostic.empty());
    REQUIRE(KeireEditor::PublishUiToolkitAuthoringAsset(
        assets, panelSettings, Keire::UiPanelSettingsAsset::StaticType(), panelSettingsSource, diagnostic));
    CHECK(diagnostic.empty());
    CHECK(assets->Load<Keire::UiVisualTreeAsset>(visualTree).TryGetLoaded());
    CHECK(assets->Load<Keire::UiStyleSheetAsset>(styleSheet).TryGetLoaded());
    CHECK(assets->Load<Keire::UiPanelSettingsAsset>(panelSettings).TryGetLoaded());

    CHECK_FALSE(KeireEditor::PublishUiToolkitAuthoringAsset(assets, Keire::AssetId::Generate(),
                                                            Keire::AssetTypeId(Keire::AssetId::Generate()),
                                                            visualTreeSource, diagnostic));
    CHECK(diagnostic.find("not a UI document") != std::string::npos);
    assets->Close();
}

TEST_CASE("UI Builder enumerates registered custom controls and authors their stable type names")
{
    constexpr std::string_view typeName = "EditorTests.FeatureCard";
    (void)Keire::Ui::UxmlElementRegistry::Register(
        {.Name = std::string(typeName),
         .Factory = [] { return Keire::Ref<Keire::Ui::VisualElement>(Keire::CreateRef<Keire::Ui::Button>()); },
         .Attributes = {{"heading", "System.String"}, {"expanded", "System.Boolean"}}});
    const auto registered = Keire::Ui::UxmlElementRegistry::Snapshot();
    const auto descriptor = std::ranges::find(registered, typeName, &Keire::Ui::UxmlElementDescriptor::Name);
    REQUIRE(descriptor != registered.end());
    CHECK(descriptor->Attributes.size() == 2);

    const auto definition = TestDocument();
    KeireEditor::UiBuilderDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000261"), definition, 1, "Custom.keireui");
    const auto custom = document.AddCustomElement(definition.Root.StableId, std::string(typeName));
    REQUIRE(document.Find(custom));
    CHECK(document.Find(custom)->Type == Keire::UiVisualElementType::Custom);
    CHECK(document.Find(custom)->CustomType == typeName);
    CHECK(document.SourcePreview().find("EditorTests.FeatureCard") != std::string::npos);
}

TEST_CASE("UI Builder style selector and declaration edits are transactional across undo save and reload")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-UiBuilderStyle-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    const auto source = root / "Hud.keirestyle";
    constexpr std::string_view originalSource =
        "@keire-style 1;\n/* Preserve this authored note. */\nLabel { color: #ffffffff; }\n";
    Keire::Detail::WriteTextFileAtomically(source, originalSource);
    const auto original = Keire::UiStyleSheetAsset::ParseSource(AsBytes(originalSource));
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "UI Style"});
    KeireEditor::UiBuilderStyleSheetDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000262"), original, 1, source, undo);

    REQUIRE(document.AddRule("Button.primary:hover", "background-color: #3366ffff;\nopacity: 0.8;"));
    REQUIRE(document.Selection());
    CHECK(document.Definition().Rules[*document.Selection()].Selector == "Button.primary:hover");
    CHECK(document.RuleDeclarations(*document.Selection()).find("opacity: 0.8;") != std::string::npos);
    const auto addedRule = *document.Selection();
    REQUIRE(document.EditRule(addedRule, "#confirm:active", "width: 240px;\nheight: 48px;"));
    CHECK(document.Definition().Rules[addedRule].Selector == "#confirm:active");
    CHECK(document.Undo());
    CHECK(document.Definition().Rules[addedRule].Selector == "Button.primary:hover");
    CHECK(document.Redo());
    CHECK(document.Definition().Rules[addedRule].Selector == "#confirm:active");

    const auto beforeInvalid = Keire::UiStyleSheetAsset::Encode(document.Definition());
    CHECK_THROWS((void)document.EditRule(addedRule, "Button[", "color: red;"));
    CHECK(Keire::UiStyleSheetAsset::Encode(document.Definition()) == beforeInvalid);
    document.Save();
    CHECK_FALSE(document.Dirty());
    const auto saved = Keire::Detail::ReadTextFile(source, Keire::MaximumUiDocumentBytes);
    CHECK(saved.find("#confirm:active") != std::string::npos);
    CHECK(saved.find("width: 240px") != std::string::npos);
    document.ReloadFromSource();
    CHECK(document.Definition().Rules.size() == 2);
    CHECK_FALSE(undo->CanUndo());
    REQUIRE(document.RemoveRule(addedRule));
    CHECK(document.Definition().Rules.size() == 1);
    CHECK(document.Undo());
    CHECK(document.Definition().Rules.size() == 2);

    document.Close();
    undoService->Close();
    std::filesystem::remove_all(root, error);
}

TEST_CASE("UI Builder style drafts preserve comments and keep the last valid preview on source errors")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-UiBuilderStyle-Draft-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    const auto source = root / "Responsive.keirestyle";
    constexpr std::string_view originalSource = R"css(@keire-style 2;

/* Brand controls are shared with the pause menu. */
.primary {
  /* Keep this comment beside the authored value. */
  color: #ffffffff;
}
)css";
    Keire::Detail::WriteTextFileAtomically(source, originalSource);
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "UI Style Draft"});
    KeireEditor::UiBuilderStyleSheetDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000263"),
                  Keire::UiStyleSheetAsset::ParseSource(AsBytes(originalSource)), 1, source, undo);

    REQUIRE(document.SetProperty(0, "color", "#3366ccff"));
    CHECK(document.SourceText().find("Brand controls") != std::string::npos);
    CHECK(document.SourceText().find("Keep this comment") != std::string::npos);
    CHECK(document.SourceText().find("color: #3366ccff") != std::string::npos);
    REQUIRE(document.SetSelector(0, "Button.primary:hover"));
    CHECK(document.SourceText().find("Button.primary:hover") != std::string::npos);
    CHECK(document.SourceText().find("Keep this comment") != std::string::npos);

    const auto lastValid = Keire::UiStyleSheetAsset::Encode(document.Definition());
    REQUIRE(document.ApplySourceDraft("@keire-style 2;\nButton { color: ;\n"));
    CHECK_FALSE(document.SourceValid());
    REQUIRE(document.SourceDiagnostic());
    CHECK(document.SourceDiagnostic()->Line > 0);
    CHECK(document.SourceDiagnostic()->Column > 0);
    CHECK(Keire::UiStyleSheetAsset::Encode(document.Definition()) == lastValid);
    CHECK_THROWS_WITH_AS(document.Save(), doctest::Contains("Repair"), std::logic_error);

    REQUIRE(document.Undo());
    CHECK(document.SourceValid());
    CHECK(document.SourceText().find("Button.primary:hover") != std::string::npos);
    document.Save();
    CHECK_FALSE(document.ExternalConflict());
    Keire::Detail::WriteTextFileAtomically(source, "@keire-style 2;\nLabel { opacity: 0.5; }\n");
    CHECK(document.ExternalConflict());
    const auto comparison = document.ExternalComparison();
    CHECK(comparison.find("--- unsaved draft") != std::string::npos);
    CHECK(comparison.find("+++ disk source") != std::string::npos);
    CHECK(comparison.find("Button.primary:hover") != std::string::npos);
    CHECK(comparison.find("+2 | Label { opacity: 0.5; }") != std::string::npos);
    CHECK_THROWS_WITH_AS(document.Save(), doctest::Contains("changed on disk"), std::logic_error);
    const auto conflictCopy = root / "Responsive Conflict Copy.keirestyle";
    document.SaveAs(conflictCopy);
    CHECK(document.ExternalConflict());
    CHECK(Keire::Detail::ReadTextFile(source, Keire::MaximumUiDocumentBytes).find("opacity: 0.5") != std::string::npos);
    CHECK(Keire::Detail::ReadTextFile(conflictCopy, Keire::MaximumUiDocumentBytes).find("Button.primary:hover") !=
          std::string::npos);
    CHECK_THROWS_WITH_AS(document.SaveAs(conflictCopy), doctest::Contains("will not overwrite"), std::invalid_argument);

    document.Close();
    undoService->Close();
    std::filesystem::remove_all(root, error);
}

TEST_CASE("UI style source editor provides navigation completion formatting and exact replacements")
{
    KeireEditor::UiStyleSourceEditor editor;
    editor.SetSource(R"css(@keire-style 2;
/* Preserve authored intent. */
Button.primary:hover {
color: var(--brand);
opac
}
)css");
    CHECK(editor.LineCount() == 7U);
    CHECK(std::ranges::any_of(editor.Tokens(), [](const auto& token)
                              { return token.Kind == KeireEditor::UiStyleSourceTokenKind::Comment; }));
    REQUIRE(editor.GoToRule("Button.primary:hover"));
    const auto ruleLocation = editor.CursorLocation();
    CHECK(ruleLocation.Line == 3U);
    const auto brace = editor.Source().find('{');
    REQUIRE(brace != std::string::npos);
    REQUIRE(editor.MatchingBrace(brace));
    CHECK(editor.Source()[*editor.MatchingBrace(brace)] == '}');

    const auto incomplete = editor.Source().find("opac") + 4U;
    const auto completions = editor.Completions(incomplete);
    const auto opacity = std::ranges::find(completions, "opacity", &KeireEditor::UiStyleSourceCompletion::Label);
    REQUIRE(opacity != completions.end());
    REQUIRE(editor.ApplyCompletion(incomplete, *opacity));
    CHECK(editor.Source().find("opacity: ") != std::string::npos);
    const auto property = editor.Source().find("opacity");
    REQUIRE(editor.HoverDocumentation(property));
    CHECK(editor.HoverDocumentation(property)->find("Default") != std::string::npos);
    CHECK(editor.ReplaceAll("--brand", "--accent", true) == 1U);
    CHECK(editor.Source().find("var(--accent)") != std::string::npos);
    REQUIRE(editor.Format());
    CHECK(editor.Source().find("  color: var(--accent);") != std::string::npos);
}

TEST_CASE("UI token refactors preview every source and reject stale previews before applying")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-UiTokenRefactor-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    const auto stylePath = root / "Theme.keirestyle";
    const auto documentPath = root / "Hud.keireui";
    constexpr std::string_view styleSource = R"css(@keire-style 2;
:root { --brand: #3366ccff; }
.primary { color: var(--brand); background-color: var(--brand); }
)css";
    constexpr std::string_view documentSource = R"xml(<?xml version="1.0" encoding="utf-8"?>
<ui schemaVersion="1" name="Hud">
  <Label id="ed170000-0000-4000-8000-000000000291" name="title" style="color: var(--brand);"/>
</ui>
)xml";
    Keire::Detail::WriteTextFileAtomically(stylePath, styleSource);
    Keire::Detail::WriteTextFileAtomically(documentPath, documentSource);
    const std::array inputs{
        KeireEditor::UiStyleTokenRefactorInput{Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000292"),
                                               Keire::UiStyleSheetAsset::StaticType(), "Theme.keirestyle"},
        KeireEditor::UiStyleTokenRefactorInput{Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000293"),
                                               Keire::UiVisualTreeAsset::StaticType(), "Hud.keireui"}};

    const auto stale = KeireEditor::BuildUiStyleTokenRefactorPreview(root, inputs, "--brand", "--accent");
    CHECK(stale.Changes.size() == 2U);
    CHECK(stale.OccurrenceCount == 4U);
    REQUIRE_FALSE(stale.Changes.front().Occurrences.empty());
    CHECK(stale.Changes.front().Occurrences.front().Line > 0U);
    CHECK_FALSE(stale.Changes.front().Occurrences.front().Preview.empty());
    Keire::Detail::WriteTextFileAtomically(documentPath, std::string(documentSource) + "\n");
    CHECK_THROWS_WITH_AS(KeireEditor::ApplyUiStyleTokenRefactor(stale), doctest::Contains("changed after preview"),
                         std::runtime_error);
    CHECK(Keire::Detail::ReadTextFile(stylePath, Keire::MaximumUiDocumentBytes).find("--brand") != std::string::npos);

    const auto current = KeireEditor::BuildUiStyleTokenRefactorPreview(root, inputs, "--brand", "--accent");
    KeireEditor::ApplyUiStyleTokenRefactor(current);
    CHECK(Keire::Detail::ReadTextFile(stylePath, Keire::MaximumUiDocumentBytes).find("--brand") == std::string::npos);
    CHECK(Keire::Detail::ReadTextFile(documentPath, Keire::MaximumUiDocumentBytes).find("--brand") ==
          std::string::npos);
    std::filesystem::remove_all(root, error);
}

TEST_CASE("UI Builder upgrades style schema only when a v2 property is authored")
{
    const auto root = std::filesystem::temp_directory_path() / "Keire-UiBuilderStyle-Schema-Test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    const auto source = root / "Legacy.keirestyle";
    constexpr std::string_view originalSource =
        "@keire-style 1;\n/* Preserve this authored note. */\nLabel { color: #ffffffff; }\n";
    Keire::Detail::WriteTextFileAtomically(source, originalSource);

    KeireEditor::UiBuilderStyleSheetDocument document;
    document.Open(Keire::AssetId::Parse("ed170000-0000-4000-8000-000000000264"),
                  Keire::UiStyleSheetAsset::ParseSource(AsBytes(originalSource)), 1, source);
    REQUIRE(document.SetProperty(0, "opacity", "0.75"));
    REQUIRE(document.SourceText().find("Preserve this authored note") != std::string::npos);
    CHECK(document.Definition().SchemaVersion == 1U);
    REQUIRE(document.SetProperty(0, "font-family", "asset(10000000-0000-0000-0000-000000000030)"));
    REQUIRE(document.SourceText().find("Preserve this authored note") != std::string::npos);
    CHECK(document.Definition().SchemaVersion == 2U);
    CHECK(document.SourceText().starts_with("@keire-style 2;"));
    Keire::UiStyleMediaCondition media;
    media.MaximumWidth = 960.0F;
    media.Orientation = Keire::UiStyleOrientation::Portrait;
    REQUIRE(document.SetMediaCondition(0, media));
    CHECK(document.SourceText().find("@media (max-width: 960px) and (orientation: portrait)") != std::string::npos);
    CHECK(document.SourceText().find("Preserve this authored note") != std::string::npos);
    REQUIRE(document.DuplicateRule(0));
    const auto duplicated = Keire::UiStyleSheetAsset::ParseSource(AsBytes(document.SourceText()));
    REQUIRE(duplicated.Rules.size() == 2U);
    CHECK(duplicated.Rules[0].Media == media);
    CHECK(duplicated.Rules[1].Media == media);
    REQUIRE(document.RemoveRule(1));
    REQUIRE(document.SetMediaCondition(0, std::nullopt));
    CHECK(document.SourceText().find("@media") == std::string::npos);
    CHECK(document.SourceText().find("Preserve this authored note") != std::string::npos);
    document.Save();
    CHECK(Keire::UiStyleSheetAsset::ParseSource(
              AsBytes(Keire::Detail::ReadTextFile(source, Keire::MaximumUiDocumentBytes)))
              .SchemaVersion == 2U);

    document.Close();
    std::filesystem::remove_all(root, error);
}
