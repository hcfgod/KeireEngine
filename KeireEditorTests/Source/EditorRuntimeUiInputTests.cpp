#include "Keire/Core.h"

#include "KeireClient/Editor/EditorRuntimeUiInput.h"

#include <doctest/doctest.h>

#include <array>
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

    [[nodiscard]] PresentationButton CreatePresentationButton(const Keire::Ref<Keire::AssetSystem>& assets,
                                                              const std::string_view name)
    {
        const std::string label(name);
        auto scene =
            Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(label));
        auto canvas = scene->CreateEntity(label + " Canvas");
        const auto canvasComponent = canvas.AddComponent<Keire::CanvasComponent>();
        if (!canvasComponent)
            throw std::runtime_error("Editor runtime UI routing fixture could not create a Canvas.");
        canvasComponent->SetScaleMode(Keire::CanvasScaleMode::ConstantPixels);

        auto button = scene->CreateEntity(label + " Button", canvas);
        const auto rect = button.AddComponent<Keire::RectTransformComponent>();
        if (!rect)
            throw std::runtime_error("Editor runtime UI routing fixture could not create a Rect Transform.");
        rect->SetAnchorMinimum({});
        rect->SetAnchorMaximum({});
        rect->SetPivot({});
        rect->SetSizeDelta({100.0F, 50.0F});
        if (!button.AddComponent<Keire::UiButtonComponent>())
            throw std::runtime_error("Editor runtime UI routing fixture could not create a Button.");

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
} // namespace

TEST_CASE("Editor runtime UI cancellation releases every captured pointer button and hover owner")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
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
