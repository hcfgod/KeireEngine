#include "Keire/Core.h"

#include "KeireRuntimeInternal/RuntimeUiInput.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>

#include <doctest/doctest.h>

#include <ranges>
#include <string>

namespace
{
    struct RoutedElement final
    {
        Keire::AssetId StableId;
        std::uint64_t DocumentGeneration = 0;
        std::uint64_t Element = 0;
    };

    struct RoutedPresentation final
    {
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        Keire::EntityId Document;
        RoutedElement Button;
        RoutedElement Scroll;
        RoutedElement Input;
    };

    [[nodiscard]] Keire::UiVisualElementDefinition Control(const Keire::UiVisualElementType type,
                                                           const std::string& name, const Keire::Vector2 position,
                                                           const Keire::Vector2 size)
    {
        Keire::UiVisualElementDefinition result;
        result.StableId = Keire::AssetId::Generate();
        result.Type = type;
        result.Name = name;
        result.InlineStyles = {{"position", "absolute"},
                               {"left", std::to_string(position.X)},
                               {"top", std::to_string(position.Y)},
                               {"width", std::to_string(size.X)},
                               {"height", std::to_string(size.Y)}};
        return result;
    }

    [[nodiscard]] RoutedElement Resolve(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                        const Keire::EntityId document, const Keire::AssetId stableId)
    {
        const auto element = presentation->FindUiDocumentElement(document, stableId);
        if (!element)
            throw std::runtime_error("Runtime UI routing fixture could not resolve a UI Document element.");
        return {.StableId = stableId, .DocumentGeneration = element->DocumentGeneration, .Element = element->Element};
    }

    [[nodiscard]] RoutedPresentation CreatePresentation(const Keire::Ref<Keire::AssetSystem>& assets,
                                                        const std::string& name, const bool lowerTree)
    {
        const auto visualTree = Keire::AssetId::Generate();
        const auto panelSettings = Keire::AssetId::Generate();
        Keire::UiVisualTreeDefinition definition;
        definition.Name = name;
        definition.Root.StableId = Keire::AssetId::Generate();
        definition.Root.InlineStyles = {{"width", "320"}, {"height", "240"}};

        auto button = Control(Keire::UiVisualElementType::Button, "button",
                              lowerTree ? Keire::Vector2{10.0F, 10.0F} : Keire::Vector2{100.0F, 10.0F}, {50.0F, 50.0F});
        button.Attributes = {{"text", name}};
        const auto buttonId = button.StableId;
        definition.Root.Children.push_back(std::move(button));

        Keire::AssetId scrollId;
        if (lowerTree)
        {
            auto scroll = Control(Keire::UiVisualElementType::ScrollView, "scroll", {10.0F, 80.0F}, {100.0F, 100.0F});
            scroll.Attributes = {{"content-width", "100"},
                                 {"content-height", "300"},
                                 {"scroll-sensitivity", "40"},
                                 {"interactable", "true"}};
            scrollId = scroll.StableId;
            definition.Root.Children.push_back(std::move(scroll));
        }
        else
        {
            auto overlay =
                Control(Keire::UiVisualElementType::Button, "wheel-overlay", {10.0F, 80.0F}, {100.0F, 100.0F});
            overlay.Attributes = {{"text", "Overlay"}};
            definition.Root.Children.push_back(std::move(overlay));
        }

        auto input = Control(Keire::UiVisualElementType::TextField, "input", {170.0F, 10.0F}, {120.0F, 40.0F});
        const auto inputId = input.StableId;
        definition.Root.Children.push_back(std::move(input));
        if (!assets->PublishDevelopmentAsset(visualTree, Keire::CreateRef<Keire::UiVisualTreeAsset>(definition)))
            throw std::runtime_error("Runtime UI routing fixture could not publish its visual tree.");
        Keire::UiPanelSettingsDefinition panelDefinition;
        panelDefinition.ScaleMode = Keire::RuntimeUiScaleMode::ConstantPixels;
        if (!assets->PublishDevelopmentAsset(panelSettings,
                                             Keire::CreateRef<Keire::UiPanelSettingsAsset>(std::move(panelDefinition))))
            throw std::runtime_error("Runtime UI routing fixture could not publish its Panel Settings.");
        const auto panelHandle = assets->Load<Keire::UiPanelSettingsAsset>(panelSettings, Keire::AssetPriority::High);
        if (!panelHandle.TryGetLoaded())
            throw std::runtime_error("Runtime UI routing fixture Panel Settings did not become ready.");

        auto scene =
            Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(name));
        auto documentEntity = scene->CreateEntity(name + " UI Document");
        const auto document = documentEntity.AddComponent<Keire::UiDocumentComponent>();
        if (!document)
            throw std::runtime_error("Runtime UI routing fixture could not create a UI Document.");
        document->SetVisualTree(visualTree);
        document->SetPanelSettings(panelSettings);

        auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets);
        session->Play();
        if (session->State() != Keire::ScenePlayState::Playing)
            throw std::runtime_error("Runtime UI routing fixture could not enter Play Mode.");
        const auto presentation = session->Presentation();
        presentation->Synchronize(session->RuntimeScene(), 320.0F, 240.0F, true);
        return {.Session = std::move(session),
                .Document = documentEntity.Id(),
                .Button = Resolve(presentation, documentEntity.Id(), buttonId),
                .Scroll = scrollId ? Resolve(presentation, documentEntity.Id(), scrollId) : RoutedElement{},
                .Input = Resolve(presentation, documentEntity.Id(), inputId)};
    }

    [[nodiscard]] std::optional<Keire::ScenePresentationUiDocumentDebugState> State(const RoutedPresentation& routed,
                                                                                    const RoutedElement& element)
    {
        const auto presentation =
            routed.Session ? routed.Session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{};
        const auto snapshot = presentation ? presentation->UiDocumentDebugSnapshot(routed.Document) : std::nullopt;
        if (!snapshot || snapshot->DocumentGeneration != element.DocumentGeneration)
            return std::nullopt;
        const auto found = std::ranges::find(snapshot->Elements, element.StableId,
                                             &Keire::ScenePresentationUiDocumentDebugElement::StableId);
        return found == snapshot->Elements.end()
                   ? std::nullopt
                   : std::optional<Keire::ScenePresentationUiDocumentDebugState>(found->State);
    }

    [[nodiscard]] SDL_Event PointerButtonEvent(const std::uint32_t type, const std::uint8_t button, const float x,
                                               const float y) noexcept
    {
        SDL_Event event{};
        event.type = type;
        event.button.button = button;
        event.button.x = x;
        event.button.y = y;
        return event;
    }
} // namespace

TEST_CASE("runtime UI stack preserves per-button capture, fallthrough, and focus-loss cancellation")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    assetSpecification.Decoders.push_back(Keire::CreateUiPanelSettingsAssetDecoder());
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto scenes = Keire::CreateRef<Keire::SceneSystem>(
        Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, assets);
    const auto world = Keire::CreateRef<Keire::SceneRuntimeWorld>(
        Keire::SceneRuntimeWorldSpecification{.Scenes = scenes, .Assets = assets});
    const auto lower = CreatePresentation(assets, "Lower", true);
    const auto upper = CreatePresentation(assets, "Upper", false);
    const auto lowerHandle = world->Adopt(lower.Session);
    const auto upperHandle = world->Adopt(upper.Session);
    REQUIRE(lowerHandle);
    REQUIRE(upperHandle);

    KeireRuntime::RuntimeUiPointerState pointer;
    auto event = PointerButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 20.0F, 20.0F);
    CHECK(KeireRuntime::ProcessRuntimeUiEventStack(world, {}, event, 1.0F, 1.0F, pointer) ==
          lower.Session->Presentation());
    CHECK(pointer.PointerCaptures[0] == lower.Session->Presentation());
    CHECK_FALSE(pointer.PointerCaptures[1]);

    event = PointerButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 110.0F, 20.0F);
    (void)KeireRuntime::ProcessRuntimeUiEventStack(world, {}, event, 1.0F, 1.0F, pointer);
    CHECK(pointer.PointerCaptures[0] == lower.Session->Presentation());
    CHECK(pointer.PointerCaptures[1] == upper.Session->Presentation());

    event = PointerButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 110.0F, 20.0F);
    (void)KeireRuntime::ProcessRuntimeUiEventStack(world, {}, event, 1.0F, 1.0F, pointer);
    CHECK_FALSE(pointer.PointerCaptures[0]);
    CHECK(pointer.PointerCaptures[1] == upper.Session->Presentation());

    SDL_Event focusLost{};
    focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    (void)KeireRuntime::ProcessRuntimeUiEventStack(world, {}, focusLost, 1.0F, 1.0F, pointer);
    CHECK_FALSE(pointer.PointerCaptures[0]);
    CHECK_FALSE(pointer.PointerCaptures[1]);
    CHECK_FALSE(pointer.PointerCaptures[2]);
    CHECK_FALSE(pointer.HoveredPresentation);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.mouse_x = 20.0F;
    wheel.wheel.mouse_y = 90.0F;
    wheel.wheel.y = -1.0F;
    wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    const auto scrollBefore = State(lower, lower.Scroll);
    REQUIRE(scrollBefore);
    INFO("scroll rect " << scrollBefore->Rect.X << ',' << scrollBefore->Rect.Y << '+' << scrollBefore->Rect.Width << 'x'
                        << scrollBefore->Rect.Height << " interactive " << scrollBefore->Interactable << " content "
                        << scrollBefore->Control.ContentSize.X << 'x' << scrollBefore->Control.ContentSize.Y);
    const auto directScrollHit = lower.Session->Presentation()->HitTestUiDocument(20.0F, 90.0F);
    REQUIRE(directScrollHit);
    CHECK(directScrollHit->Document == lower.Document);
    CHECK(directScrollHit->StableId == lower.Scroll.StableId);
    (void)KeireRuntime::ProcessRuntimeUiEventStack(world, {}, wheel, 1.0F, 1.0F, pointer);
    const auto lowerScroll = State(lower, lower.Scroll);
    REQUIRE(lowerScroll);
    CHECK(lowerScroll->Style.ContentOffset.Y == doctest::Approx(lowerScroll->Control.ScrollSensitivity));
    CHECK(lower.Session->Presentation()->ConsumeUiDocumentElementEvent(lower.Document, lower.Scroll.DocumentGeneration,
                                                                       lower.Scroll.Element,
                                                                       Keire::RuntimeUiEventType::ValueChanged));
    CHECK(pointer.HoveredPresentation == lower.Session->Presentation());

    world->Close();
    scenes->Close();
    assets->Close();
}

TEST_CASE("runtime UI keyboard and text input target only the focused active presentation")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
    assetSpecification.Decoders.push_back(Keire::CreateUiVisualTreeAssetDecoder());
    assetSpecification.Decoders.push_back(Keire::CreateUiPanelSettingsAssetDecoder());
    const auto assets = Keire::CreateRef<Keire::AssetSystem>(std::move(assetSpecification));
    const auto scenes = Keire::CreateRef<Keire::SceneSystem>(
        Keire::SceneSystemSpecification{.Mode = Keire::SceneMode::Enabled}, assets);
    const auto world = Keire::CreateRef<Keire::SceneRuntimeWorld>(
        Keire::SceneRuntimeWorldSpecification{.Scenes = scenes, .Assets = assets});
    const auto lower = CreatePresentation(assets, "Lower keyboard", true);
    const auto upper = CreatePresentation(assets, "Upper keyboard", false);
    const auto lowerHandle = world->Adopt(lower.Session);
    const auto upperHandle = world->Adopt(upper.Session);
    REQUIRE(lowerHandle);
    REQUIRE(upperHandle);
    REQUIRE(world->SetActive(lowerHandle));
    world->Process();
    REQUIRE(lower.Session->Presentation()->FocusUiDocumentElement(lower.Document, lower.Input.DocumentGeneration,
                                                                  lower.Input.Element));
    REQUIRE(upper.Session->Presentation()->FocusUiDocumentElement(upper.Document, upper.Input.DocumentGeneration,
                                                                  upper.Input.Element));

    KeireRuntime::RuntimeUiPointerState pointer;
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "lower";
    CHECK(KeireRuntime::ProcessRuntimeUiEventStack(world, {}, text, 1.0F, 1.0F, pointer) ==
          lower.Session->Presentation());
    CHECK(lower.Session->Presentation()->ReadUiDocumentElementText(lower.Document, lower.Input.DocumentGeneration,
                                                                   lower.Input.Element) == "lower");
    CHECK(upper.Session->Presentation()
              ->ReadUiDocumentElementText(upper.Document, upper.Input.DocumentGeneration, upper.Input.Element)
              ->empty());

    SDL_Event backspace{};
    backspace.type = SDL_EVENT_KEY_DOWN;
    backspace.key.key = SDLK_BACKSPACE;
    (void)KeireRuntime::ProcessRuntimeUiEventStack(world, {}, backspace, 1.0F, 1.0F, pointer);
    CHECK(lower.Session->Presentation()->ReadUiDocumentElementText(lower.Document, lower.Input.DocumentGeneration,
                                                                   lower.Input.Element) == "lowe");
    CHECK(upper.Session->Presentation()
              ->ReadUiDocumentElementText(upper.Document, upper.Input.DocumentGeneration, upper.Input.Element)
              ->empty());

    REQUIRE(world->SetActive(upperHandle));
    world->Process();
    REQUIRE(upper.Session->Presentation()->Ui()->SetFocus({}));
    text.text.text = "ignored";
    CHECK_FALSE(KeireRuntime::ProcessRuntimeUiEventStack(world, {}, text, 1.0F, 1.0F, pointer));
    CHECK(upper.Session->Presentation()
              ->ReadUiDocumentElementText(upper.Document, upper.Input.DocumentGeneration, upper.Input.Element)
              ->empty());
    REQUIRE(upper.Session->Presentation()->FocusUiDocumentElement(upper.Document, upper.Input.DocumentGeneration,
                                                                  upper.Input.Element));
    text.text.text = "upper";
    CHECK(KeireRuntime::ProcessRuntimeUiEventStack(world, {}, text, 1.0F, 1.0F, pointer) ==
          upper.Session->Presentation());
    CHECK(lower.Session->Presentation()->ReadUiDocumentElementText(lower.Document, lower.Input.DocumentGeneration,
                                                                   lower.Input.Element) == "lowe");
    CHECK(upper.Session->Presentation()->ReadUiDocumentElementText(upper.Document, upper.Input.DocumentGeneration,
                                                                   upper.Input.Element) == "upper");

    world->Close();
    scenes->Close();
    assets->Close();
}
