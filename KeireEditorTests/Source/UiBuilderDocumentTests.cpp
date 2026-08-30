#include "KeireClient/Editor/EditorPanels.h"
#include "KeireClient/Editor/UiBuilderDocument.h"
#include "KeireClient/Editor/UiBuilderLiveDraft.h"
#include "KeireClient/Editor/UiBuilderStyleSheetDocument.h"

#include "Keire/ECS/Components/UiDocumentComponent.h"
#include "Keire/Ui/UiElements.h"
#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <limits>
#include <span>
#include <string>

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
    constexpr std::string_view originalSource = "@keire-style 1;\nLabel { color: #ffffffff; }\n";
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
