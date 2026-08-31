#include "Keire/Core.h"

#include "KeireClient/Editor/EditorRuntimeUiInput.h"

#include <array>
#include <doctest/doctest.h>
#include <string>
#include <string_view>

namespace
{
    struct PresentationButton final
    {
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        Keire::Ref<Keire::ScenePresentationRuntime> Presentation;
        Keire::RuntimeUiElementId Button;
    };

    struct PresentationSlider final
    {
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        Keire::Ref<Keire::ScenePresentationRuntime> Presentation;
        Keire::EntityId Document;
        Keire::ScenePresentationUiDocumentElement Slider;
    };

    [[nodiscard]] PresentationButton CreatePresentationButton(const Keire::Ref<Keire::AssetSystem>& assets,
                                                              const std::string_view name)
    {
        const std::string label(name);
        const auto visualTree = Keire::AssetId::Generate();
        Keire::UiVisualTreeDefinition definition;
        definition.Name = label;
        definition.Root.StableId = Keire::AssetId::Generate();
        definition.Root.InlineStyles = {{"width", "200"}, {"height", "100"}};
        Keire::UiVisualElementDefinition buttonDefinition;
        buttonDefinition.StableId = Keire::AssetId::Generate();
        buttonDefinition.Type = Keire::UiVisualElementType::Button;
        buttonDefinition.Name = "action";
        buttonDefinition.Attributes = {{"text", label}};
        buttonDefinition.InlineStyles = {{"width", "100"}, {"height", "50"}};
        definition.Root.Children.push_back(std::move(buttonDefinition));
        if (!assets->PublishDevelopmentAsset(visualTree, Keire::CreateRef<Keire::UiVisualTreeAsset>(definition)))
            throw std::runtime_error("Editor runtime UI routing fixture could not publish its visual tree.");
        const auto panelSettings = Keire::AssetId::Generate();
        Keire::UiPanelSettingsDefinition panelDefinition;
        panelDefinition.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
        if (!assets->PublishDevelopmentAsset(panelSettings,
                                             Keire::CreateRef<Keire::UiPanelSettingsAsset>(std::move(panelDefinition))))
            throw std::runtime_error("Editor runtime UI routing fixture could not publish its Panel Settings.");
        if (!assets->Load<Keire::UiPanelSettingsAsset>(panelSettings, Keire::AssetPriority::High).TryGetLoaded())
            throw std::runtime_error("Editor runtime UI routing fixture Panel Settings did not become ready.");

        auto scene =
            Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(label));
        auto documentEntity = scene->CreateEntity(label + " UI Document");
        const auto document = documentEntity.AddComponent<Keire::UiDocumentComponent>();
        if (!document)
            throw std::runtime_error("Editor runtime UI routing fixture could not create a UI Document.");
        document->SetVisualTree(visualTree);
        document->SetPanelSettings(panelSettings);

        auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(std::move(scene), assets);
        session->Play();
        if (session->State() != Keire::ScenePlayState::Playing || !session->Presentation())
            throw std::runtime_error("Editor runtime UI routing fixture could not enter Play Mode.");
        auto presentation = session->Presentation();
        presentation->Synchronize(session->RuntimeScene(), 200.0F, 100.0F, true);
        const auto hit = presentation->Ui()->HitTest(25.0F, 25.0F);
        if (!hit)
            throw std::runtime_error("Editor runtime UI routing fixture button did not participate in hit testing.");
        return {std::move(session), std::move(presentation), *hit};
    }

    [[nodiscard]] PresentationSlider CreatePresentationSlider(const Keire::Ref<Keire::AssetSystem>& assets)
    {
        const auto visualTree = Keire::AssetId::Generate();
        Keire::UiVisualTreeDefinition definition;
        definition.Name = "Slider capture";
        definition.Root.StableId = Keire::AssetId::Generate();
        definition.Root.InlineStyles = {{"width", "200"}, {"height", "100"}};
        Keire::UiVisualElementDefinition slider;
        slider.StableId = Keire::AssetId::Generate();
        slider.Type = Keire::UiVisualElementType::Slider;
        slider.Name = "volume";
        slider.Attributes = {{"minimum", "0"}, {"maximum", "100"}, {"value", "0"}};
        slider.InlineStyles = {{"width", "200"}, {"height", "100"}};
        definition.Root.Children.push_back(std::move(slider));
        if (!assets->PublishDevelopmentAsset(visualTree, Keire::CreateRef<Keire::UiVisualTreeAsset>(definition)))
            throw std::runtime_error("Editor runtime UI routing fixture could not publish its slider visual tree.");
        const auto panelSettings = Keire::AssetId::Generate();
        Keire::UiPanelSettingsDefinition panelDefinition;
        panelDefinition.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
        if (!assets->PublishDevelopmentAsset(panelSettings,
                                             Keire::CreateRef<Keire::UiPanelSettingsAsset>(std::move(panelDefinition))))
            throw std::runtime_error("Editor runtime UI routing fixture could not publish its slider Panel Settings.");

        auto scene = Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(),
                                                    Keire::SceneAsset::EmptyDefinition("Slider capture"));
        auto documentEntity = scene->CreateEntity("Slider document");
        const auto document = documentEntity.AddComponent<Keire::UiDocumentComponent>();
        if (!document)
            throw std::runtime_error("Editor runtime UI routing fixture could not create its slider document.");
        document->SetVisualTree(visualTree);
        document->SetPanelSettings(panelSettings);
        auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(std::move(scene), assets);
        session->Play();
        if (session->State() != Keire::ScenePlayState::Playing || !session->Presentation())
            throw std::runtime_error("Editor runtime UI routing fixture could not enter Play Mode.");
        auto presentation = session->Presentation();
        presentation->Synchronize(session->RuntimeScene(), 200.0F, 100.0F, true);
        const auto runtimeSlider = presentation->FindUiDocumentElement(documentEntity.Id(), "volume");
        if (!runtimeSlider)
            throw std::runtime_error("Editor runtime UI routing fixture could not resolve its slider.");
        return {std::move(session), std::move(presentation), documentEntity.Id(), *runtimeSlider};
    }
} // namespace

TEST_CASE("Editor runtime UI cancellation releases every captured pointer button and hover owner")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    assetSpecification.Decoders.push_back(Keire::CreateUiPanelSettingsAssetDecoder());
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto first = CreatePresentationButton(assets, "First");
    const auto second = CreatePresentationButton(assets, "Second");
    REQUIRE(first.Presentation->Ui()->HitTest(25.0F, 25.0F) == first.Button);
    REQUIRE(second.Presentation->Ui()->HitTest(25.0F, 25.0F) == second.Button);
    REQUIRE(first.Presentation->PointerButton(25.0F, 25.0F, Keire::RuntimeUiPointerButton::Primary, true));
    REQUIRE(second.Presentation->PointerButton(25.0F, 25.0F, Keire::RuntimeUiPointerButton::Secondary, true));
    REQUIRE(first.Presentation->Ui()->State(first.Button));
    REQUIRE(second.Presentation->Ui()->State(second.Button));
    CHECK(first.Presentation->Ui()->State(first.Button)->Pressed);
    CHECK(second.Presentation->Ui()->State(second.Button)->Pressed);

    KeireEditor::RuntimeUiPointerRoutingState state;
    state.HoveredPresentation = second.Presentation;
    state.PointerCaptures[0] = first.Presentation;
    state.PointerCaptures[1] = second.Presentation;
    const std::array presentations{first.Presentation, second.Presentation};
    KeireEditor::CancelRuntimeUiPointer(presentations, state);

    CHECK_FALSE(state.HoveredPresentation);
    CHECK_FALSE(state.PointerCaptures[0]);
    CHECK_FALSE(state.PointerCaptures[1]);
    CHECK_FALSE(state.PointerCaptures[2]);
    REQUIRE(first.Presentation->Ui()->State(first.Button));
    REQUIRE(second.Presentation->Ui()->State(second.Button));
    CHECK_FALSE(first.Presentation->Ui()->State(first.Button)->Pressed);
    CHECK_FALSE(second.Presentation->Ui()->State(second.Button)->Pressed);
    first.Session->Stop();
    second.Session->Stop();
    first.Session->EditScene()->Close();
    second.Session->EditScene()->Close();
    assets->Close();
}

TEST_CASE("Editor keyboard routing selects only an active presentation with retained UI focus")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    assetSpecification.Decoders.push_back(Keire::CreateUiPanelSettingsAssetDecoder());
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto active = CreatePresentationButton(assets, "Keyboard active");
    CHECK_FALSE(KeireEditor::SelectRuntimeUiKeyboardPresentation(active.Presentation));
    REQUIRE(active.Presentation->Ui()->SetFocus(active.Button));
    CHECK(KeireEditor::SelectRuntimeUiKeyboardPresentation(active.Presentation) == active.Presentation);
    REQUIRE(active.Presentation->Ui()->SetFocus({}));
    CHECK_FALSE(KeireEditor::SelectRuntimeUiKeyboardPresentation(active.Presentation));
    active.Session->Stop();
    active.Session->EditScene()->Close();
    assets->Close();
}

TEST_CASE("Editor runtime UI presentation hit testing uses the presentation projection")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    assetSpecification.Decoders.push_back(Keire::CreateUiPanelSettingsAssetDecoder());
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto fixture = CreatePresentationButton(assets, "Projected hit");
    CHECK(KeireEditor::RuntimeUiPresentationHitTest(fixture.Presentation, 25.0F, 25.0F));
    CHECK_FALSE(KeireEditor::RuntimeUiPresentationHitTest(fixture.Presentation, 250.0F, 150.0F));
    CHECK_FALSE(KeireEditor::RuntimeUiPresentationHitTest({}, 25.0F, 25.0F));
    fixture.Session->Stop();
    fixture.Session->EditScene()->Close();
    assets->Close();
}

TEST_CASE("Editor world-surface UI routes projected focus text and submit input")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    assetSpecification.Decoders.push_back(Keire::CreateUiPanelSettingsAssetDecoder());
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto visualTree = Keire::AssetId::Generate();
    const auto panelSettings = Keire::AssetId::Generate();
    const auto fieldId = Keire::AssetId::Generate();
    Keire::UiVisualTreeDefinition definition;
    definition.Name = "World input";
    definition.Root.StableId = Keire::AssetId::Generate();
    definition.Root.InlineStyles = {{"width", "400"}, {"height", "200"}};
    Keire::UiVisualElementDefinition field;
    field.StableId = fieldId;
    field.Type = Keire::UiVisualElementType::TextField;
    field.Name = "command";
    field.InlineStyles = {{"width", "400"}, {"height", "200"}};
    definition.Root.Children.push_back(std::move(field));
    REQUIRE(assets->PublishDevelopmentAsset(visualTree, Keire::CreateRef<Keire::UiVisualTreeAsset>(definition)));
    Keire::UiPanelSettingsDefinition panel;
    panel.Target = Keire::UiPanelTarget::WorldSurface;
    panel.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
    panel.ReferenceWidth = 400.0F;
    panel.ReferenceHeight = 200.0F;
    panel.WorldWidth = 2.0F;
    panel.WorldHeight = 1.0F;
    REQUIRE(assets->PublishDevelopmentAsset(panelSettings, Keire::CreateRef<Keire::UiPanelSettingsAsset>(panel)));

    auto scene =
        Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition("World input"));
    auto camera = scene->CreateEntity("Camera");
    REQUIRE(camera.AddComponent<Keire::CameraComponent>());
    camera.GetComponent<Keire::TransformComponent>()->SetLocalPosition({0.0F, 0.0F, -3.0F});
    auto documentEntity = scene->CreateEntity("World terminal");
    const auto document = documentEntity.AddComponent<Keire::UiDocumentComponent>();
    REQUIRE(document);
    document->SetVisualTree(visualTree);
    document->SetPanelSettings(panelSettings);

    const auto presentation =
        Keire::CreateRef<Keire::ScenePresentationRuntime>(assets, Keire::Ref<Keire::AudioSystem>{});
    presentation->Synchronize(scene, 400.0F, 200.0F, true);
    const auto canvas = presentation->CanvasGeometry(documentEntity.Id());
    REQUIRE(canvas);
    REQUIRE(canvas->Visible);
    Keire::Vector2 center;
    for (const auto point : canvas->ViewportCorners)
    {
        center.X += point.X * 0.25F;
        center.Y += point.Y * 0.25F;
    }
    CHECK(KeireEditor::RuntimeUiPresentationHitTest(presentation, center.X, center.Y));
    const auto command = presentation->FindUiDocumentElement(documentEntity.Id(), fieldId);
    REQUIRE(command);
    REQUIRE(presentation->PointerButton(center.X, center.Y, Keire::RuntimeUiPointerButton::Primary, true));
    REQUIRE(presentation->PointerButton(center.X, center.Y, Keire::RuntimeUiPointerButton::Primary, false));
    CHECK(presentation->TextInputFocused());
    CHECK(KeireEditor::RequestRuntimeUiTextInput(presentation));
    presentation->TextInput("status");
    CHECK(presentation->ReadUiDocumentElementText(documentEntity.Id(), command->DocumentGeneration, command->Element) ==
          "status");
    REQUIRE(presentation->KeyInput(Keire::RuntimeUiKey::Enter));
    CHECK(presentation->ConsumeUiDocumentElementEvent(documentEntity.Id(), command->DocumentGeneration,
                                                      command->Element, Keire::RuntimeUiEventType::Submit));

    REQUIRE(presentation->Ui()->SetFocus({}));
    CHECK_FALSE(KeireEditor::RequestRuntimeUiTextInput(presentation));

    presentation->Clear();
    scene->Close();
    assets->Close();
}

TEST_CASE("Game runtime UI routes edit-mode pointers without granting keyboard ownership")
{
    const auto editMode = KeireEditor::ResolveRuntimeGameUiRouting(false, true, false);
    CHECK(editMode.Pointer);
    CHECK_FALSE(editMode.Keyboard);

    const auto playMode = KeireEditor::ResolveRuntimeGameUiRouting(true, true, true);
    CHECK(playMode.Pointer);
    CHECK(playMode.Keyboard);

    const auto unfocusedPlayMode = KeireEditor::ResolveRuntimeGameUiRouting(true, true, false);
    CHECK(unfocusedPlayMode.Pointer);
    CHECK_FALSE(unfocusedPlayMode.Keyboard);

    const auto missingPresentation = KeireEditor::ResolveRuntimeGameUiRouting(true, false, true);
    CHECK_FALSE(missingPresentation.Pointer);
    CHECK_FALSE(missingPresentation.Keyboard);
}

TEST_CASE("Game runtime UI retains slider capture while viewport input ownership remains inactive")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    assetSpecification.Decoders.push_back(Keire::CreateUiPanelSettingsAssetDecoder());
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto fixture = CreatePresentationSlider(assets);
    REQUIRE(fixture.Presentation->PointerButton(20.0F, 50.0F, Keire::RuntimeUiPointerButton::Primary, true));

    KeireEditor::RuntimeUiPointerRoutingState state;
    state.PointerCaptures[0] = fixture.Presentation;
    KeireEditor::ReconcileRuntimeUiPointerCapture({}, state, false, false);
    REQUIRE(state.PointerCaptures[0] == fixture.Presentation);
    fixture.Presentation->PointerMove(180.0F, 50.0F);
    CHECK(fixture.Presentation->ReadUiDocumentElementValue(fixture.Document, fixture.Slider.DocumentGeneration,
                                                           fixture.Slider.Element) == doctest::Approx(90.0F));

    KeireEditor::ReconcileRuntimeUiPointerCapture({}, state, false, true);
    REQUIRE(state.PointerCaptures[0] == fixture.Presentation);
    KeireEditor::ReconcileRuntimeUiPointerCapture({}, state, true, false);
    CHECK_FALSE(state.PointerCaptures[0]);
    fixture.Presentation->PointerMove(40.0F, 50.0F);
    CHECK(fixture.Presentation->ReadUiDocumentElementValue(fixture.Document, fixture.Slider.DocumentGeneration,
                                                           fixture.Slider.Element) == doctest::Approx(90.0F));

    fixture.Session->Stop();
    fixture.Session->EditScene()->Close();
    assets->Close();
}
