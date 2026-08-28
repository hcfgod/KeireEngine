#include "Keire/Core.h"

#include "KeireRuntimeInternal/RuntimeUiInput.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>

#include <doctest/doctest.h>

#include <string>

namespace
{
    struct RoutedPresentation final
    {
        Keire::Ref<Keire::SceneRuntimeSession> Session;
        Keire::EntityId Button;
        Keire::EntityId Scroll;
        Keire::EntityId Input;
    };

    [[nodiscard]] Keire::Entity CreateControl(const Keire::Ref<Keire::Scene>& scene, const Keire::Entity& canvas,
                                              const std::string& name, const Keire::Vector2 position,
                                              const Keire::Vector2 size)
    {
        auto entity = scene->CreateEntity(name, canvas);
        const auto rect = entity.AddComponent<Keire::RectTransformComponent>();
        if (!rect)
            throw std::runtime_error("Runtime UI routing fixture could not create a Rect Transform.");
        rect->SetAnchorMinimum({});
        rect->SetAnchorMaximum({});
        rect->SetPivot({});
        rect->SetAnchoredPosition(position);
        rect->SetSizeDelta(size);
        return entity;
    }

    [[nodiscard]] RoutedPresentation CreatePresentation(const Keire::Ref<Keire::AssetSystem>& assets,
                                                        const std::string& name, const bool lowerTree)
    {
        auto scene =
            Keire::CreateRef<Keire::Scene>(Keire::AssetId::Generate(), Keire::SceneAsset::EmptyDefinition(name));
        auto canvas = scene->CreateEntity(name + " Canvas");
        const auto canvasComponent = canvas.AddComponent<Keire::CanvasComponent>();
        if (!canvasComponent)
            throw std::runtime_error("Runtime UI routing fixture could not create a Canvas.");
        canvasComponent->SetScaleMode(Keire::CanvasScaleMode::ConstantPixels);

        const auto buttonPosition = lowerTree ? Keire::Vector2{10.0F, 10.0F} : Keire::Vector2{100.0F, 10.0F};
        auto button = CreateControl(scene, canvas, name + " Button", buttonPosition, {50.0F, 50.0F});
        if (!button.AddComponent<Keire::UiButtonComponent>())
            throw std::runtime_error("Runtime UI routing fixture could not create a Button.");

        Keire::EntityId scroll;
        if (lowerTree)
        {
            auto scrollEntity = CreateControl(scene, canvas, name + " Scroll", {10.0F, 80.0F}, {100.0F, 100.0F});
            const auto component = scrollEntity.AddComponent<Keire::UiScrollViewComponent>();
            if (!component)
                throw std::runtime_error("Runtime UI routing fixture could not create a Scroll View.");
            component->SetContentSize({100.0F, 300.0F});
            scroll = scrollEntity.Id();
        }
        else
        {
            auto overlay = CreateControl(scene, canvas, name + " Wheel overlay", {10.0F, 80.0F}, {100.0F, 100.0F});
            if (!overlay.AddComponent<Keire::UiButtonComponent>())
                throw std::runtime_error("Runtime UI routing fixture could not create a wheel overlay.");
        }

        auto input = CreateControl(scene, canvas, name + " Input", {170.0F, 10.0F}, {120.0F, 40.0F});
        if (!input.AddComponent<Keire::UiInputFieldComponent>())
            throw std::runtime_error("Runtime UI routing fixture could not create an Input Field.");

        auto session = Keire::CreateRef<Keire::SceneRuntimeSession>(scene, assets);
        session->Play();
        if (session->State() != Keire::ScenePlayState::Playing)
            throw std::runtime_error("Runtime UI routing fixture could not enter Play Mode.");
        session->Presentation()->Synchronize(session->RuntimeScene(), 320.0F, 240.0F, true);
        return {std::move(session), button.Id(), scroll, input.Id()};
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
    (void)KeireRuntime::ProcessRuntimeUiEventStack(world, {}, wheel, 1.0F, 1.0F, pointer);
    const auto lowerScroll =
        lower.Session->RuntimeScene()->FindEntity(lower.Scroll).GetComponent<Keire::UiScrollViewComponent>();
    REQUIRE(lowerScroll);
    CHECK(lowerScroll->Offset().Y == doctest::Approx(lowerScroll->Sensitivity()));
    CHECK(pointer.HoveredPresentation == lower.Session->Presentation());

    world->Close();
    scenes->Close();
    assets->Close();
}

TEST_CASE("runtime UI keyboard and text input target only the focused active presentation")
{
    Keire::AssetSystemSpecification assetSpecification;
    assetSpecification.Mode = Keire::AssetMode::Development;
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
    REQUIRE(lower.Session->Presentation()->SetFocus(lower.Input));
    REQUIRE(upper.Session->Presentation()->SetFocus(upper.Input));

    KeireRuntime::RuntimeUiPointerState pointer;
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "lower";
    CHECK(KeireRuntime::ProcessRuntimeUiEventStack(world, {}, text, 1.0F, 1.0F, pointer) ==
          lower.Session->Presentation());
    const auto lowerInput =
        lower.Session->RuntimeScene()->FindEntity(lower.Input).GetComponent<Keire::UiInputFieldComponent>();
    const auto upperInput =
        upper.Session->RuntimeScene()->FindEntity(upper.Input).GetComponent<Keire::UiInputFieldComponent>();
    REQUIRE(lowerInput);
    REQUIRE(upperInput);
    CHECK(lowerInput->Text() == "lower");
    CHECK(upperInput->Text().empty());

    SDL_Event backspace{};
    backspace.type = SDL_EVENT_KEY_DOWN;
    backspace.key.key = SDLK_BACKSPACE;
    (void)KeireRuntime::ProcessRuntimeUiEventStack(world, {}, backspace, 1.0F, 1.0F, pointer);
    CHECK(lowerInput->Text() == "lowe");
    CHECK(upperInput->Text().empty());

    REQUIRE(world->SetActive(upperHandle));
    world->Process();
    REQUIRE(upper.Session->Presentation()->Ui()->SetFocus({}));
    text.text.text = "ignored";
    CHECK_FALSE(KeireRuntime::ProcessRuntimeUiEventStack(world, {}, text, 1.0F, 1.0F, pointer));
    CHECK(upperInput->Text().empty());
    REQUIRE(upper.Session->Presentation()->SetFocus(upper.Input));
    text.text.text = "upper";
    CHECK(KeireRuntime::ProcessRuntimeUiEventStack(world, {}, text, 1.0F, 1.0F, pointer) ==
          upper.Session->Presentation());
    CHECK(lowerInput->Text() == "lowe");
    CHECK(upperInput->Text() == "upper");

    world->Close();
    scenes->Close();
    assets->Close();
}
