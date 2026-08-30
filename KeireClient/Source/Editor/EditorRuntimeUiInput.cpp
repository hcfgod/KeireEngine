#include "KeireClient/Editor/EditorRuntimeUiInput.h"

#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "Keire/Ui.h"
#include "Keire/Ui/RuntimeUi.h"
#include "KeireInternal/UiInputInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace
{
    constexpr std::size_t PrimaryButton = 0U;
    constexpr std::size_t SecondaryButton = 1U;
    constexpr std::size_t MiddleButton = 2U;
    constexpr std::array PointerButtons{Keire::RuntimeUiPointerButton::Primary,
                                        Keire::RuntimeUiPointerButton::Secondary,
                                        Keire::RuntimeUiPointerButton::Middle};

    [[nodiscard]] bool Contains(const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
                                const Keire::Ref<Keire::ScenePresentationRuntime>& presentation)
    {
        return presentation && std::ranges::find(presentations, presentation) != presentations.end();
    }

    void LeaveExcept(const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
                     const Keire::Ref<Keire::ScenePresentationRuntime>& target)
    {
        for (const auto& presentation : presentations)
            if (presentation && presentation != target)
                presentation->PointerLeave();
    }
} // namespace

namespace KeireEditor
{
    void CancelRuntimeUiPointer(const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
                                RuntimeUiPointerRoutingState& state) noexcept
    {
        const auto leave = [](const Keire::Ref<Keire::ScenePresentationRuntime>& presentation) noexcept
        {
            if (!presentation)
                return;
            try
            {
                presentation->PointerLeave();
            }
            catch (...)
            {
            }
        };
        for (std::size_t index = 0; index < state.PointerCaptures.size(); ++index)
        {
            const auto& capture = state.PointerCaptures[index];
            if (capture)
                (void)capture->CancelPointerButton(PointerButtons[index]);
        }
        for (const auto& presentation : presentations)
            leave(presentation);
        for (const auto& capture : state.PointerCaptures)
            if (capture && !Contains(presentations, capture))
                leave(capture);
        state.Reset();
    }

    void CancelRuntimeUiPointer(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                                RuntimeUiPointerRoutingState& state) noexcept
    {
        std::vector<Keire::Ref<Keire::ScenePresentationRuntime>> presentations;
        if (world)
        {
            for (const auto& session : world->Sessions())
                if (const auto presentation =
                        session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{})
                    presentations.push_back(presentation);
        }
        CancelRuntimeUiPointer(presentations, state);
    }

    void RouteRuntimeUiPointer(Keire::UiFrame& ui,
                               const std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
                               const Keire::UiItemRect& viewport, RuntimeUiPointerRoutingState& state)
    {
        const auto pointer = ui.PointerState();
        const float localX = pointer.Position.X - viewport.Minimum.X;
        const float localY = pointer.Position.Y - viewport.Minimum.Y;
        for (std::size_t index = 0; index < state.PointerCaptures.size(); ++index)
        {
            auto& capture = state.PointerCaptures[index];
            if (capture && !Contains(presentations, capture))
            {
                (void)capture->CancelPointerButton(PointerButtons[index]);
                capture->PointerLeave();
                capture.Reset();
            }
        }
        const auto hit = [&](const Keire::Ref<Keire::ScenePresentationRuntime>& presentation)
        { return RuntimeUiPresentationHitTest(presentation, localX, localY); };
        const auto routeMove = [&]
        {
            std::vector<Keire::Ref<Keire::ScenePresentationRuntime>> captures;
            for (const auto& capture : state.PointerCaptures)
                if (capture && std::ranges::find(captures, capture) == captures.end())
                    captures.push_back(capture);
            Keire::Ref<Keire::ScenePresentationRuntime> target =
                captures.empty() ? Keire::Ref<Keire::ScenePresentationRuntime>{} : captures.front();
            if (!target && viewport.Contains(pointer.Position))
            {
                const auto found = std::ranges::find_if(presentations.rbegin(), presentations.rend(), hit);
                if (found != presentations.rend())
                    target = *found;
            }
            for (const auto& presentation : presentations)
            {
                if (presentation && presentation != target &&
                    std::ranges::find(captures, presentation) == captures.end())
                    presentation->PointerLeave();
            }
            if (captures.empty())
            {
                if (target)
                    target->PointerMove(localX, localY);
            }
            else
            {
                for (const auto& capture : captures)
                    capture->PointerMove(localX, localY);
            }
            state.HoveredPresentation = target;
        };
        routeMove();

        const auto routeButton =
            [&](const Keire::RuntimeUiPointerButton button, const std::size_t index, const bool pressed)
        {
            if (pressed)
            {
                auto& capture = state.PointerCaptures[index];
                auto handled = capture;
                if (handled)
                {
                    if (!handled->PointerButton(localX, localY, button, true))
                        handled.Reset();
                }
                else
                {
                    for (auto current = presentations.rbegin(); current != presentations.rend(); ++current)
                    {
                        if (*current && (*current)->PointerButton(localX, localY, button, true))
                        {
                            handled = *current;
                            break;
                        }
                    }
                }
                if (handled)
                    capture = handled;
                else
                    capture.Reset();
                routeMove();
                return;
            }
            auto& capture = state.PointerCaptures[index];
            if (!capture)
                return;
            capture->PointerMove(localX, localY);
            (void)capture->PointerButton(localX, localY, button, false);
            capture.Reset();
            routeMove();
        };
        if (viewport.Contains(pointer.Position))
        {
            if (pointer.LeftPressed)
                routeButton(Keire::RuntimeUiPointerButton::Primary, PrimaryButton, true);
            if (pointer.RightPressed)
                routeButton(Keire::RuntimeUiPointerButton::Secondary, SecondaryButton, true);
            if (pointer.MiddlePressed)
                routeButton(Keire::RuntimeUiPointerButton::Middle, MiddleButton, true);
        }
        if (pointer.LeftReleased)
            routeButton(Keire::RuntimeUiPointerButton::Primary, PrimaryButton, false);
        if (pointer.RightReleased)
            routeButton(Keire::RuntimeUiPointerButton::Secondary, SecondaryButton, false);
        if (pointer.MiddleReleased)
            routeButton(Keire::RuntimeUiPointerButton::Middle, MiddleButton, false);
        if (viewport.Contains(pointer.Position) && pointer.Wheel != 0.0F)
        {
            const bool handled = std::ranges::any_of(
                presentations.rbegin(), presentations.rend(),
                [&](const Keire::Ref<Keire::ScenePresentationRuntime>& presentation)
                { return presentation && presentation->PointerWheel(localX, localY, 0.0F, pointer.Wheel); });
            if (handled)
                ui.CapturePointerWheel();
            routeMove();
        }
    }

    bool RuntimeUiPresentationHitTest(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation, const float x,
                                      const float y) noexcept
    {
        return presentation && static_cast<bool>(presentation->HitTestUiEntity(x, y));
    }

    Keire::Ref<Keire::ScenePresentationRuntime>
    SelectRuntimeUiKeyboardPresentation(const Keire::Ref<Keire::ScenePresentationRuntime>& active) noexcept
    {
        return active && active->FocusedUiEntity() ? active : Keire::Ref<Keire::ScenePresentationRuntime>{};
    }

    bool RequestRuntimeUiTextInput(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation) noexcept
    {
        if (!presentation || !presentation->TextInputFocused())
            return false;
        Keire::Detail::UiBackendRequestTextInput();
        return true;
    }

    void RouteRuntimeUiKeyboard(Keire::UiFrame& ui, const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                const Keire::Ref<Keire::WindowSystem>& windows, const Keire::Ref<Keire::Window>& window)
    {
        if (!presentation || !presentation->FocusedUiEntity())
            return;
        const bool textInputFocused = RequestRuntimeUiTextInput(presentation);
        if (windows && window)
            (void)Keire::WindowSystemInternalAccess::SetTextInput(*windows, window->Id(), textInputFocused);
        if (textInputFocused)
            presentation->TextInput(ui.TextInput());
        if (ui.KeyPressed(Keire::UiKey::Backspace))
            (void)presentation->KeyInput(Keire::RuntimeUiKey::Backspace);
        if (ui.KeyPressed(Keire::UiKey::Delete))
            (void)presentation->KeyInput(Keire::RuntimeUiKey::Delete);
        if (ui.KeyPressed(Keire::UiKey::Enter) && !presentation->KeyInput(Keire::RuntimeUiKey::Enter))
            presentation->Navigate(Keire::RuntimeUiNavigation::Accept);
        if (ui.KeyPressed(Keire::UiKey::Escape) && !presentation->KeyInput(Keire::RuntimeUiKey::Escape))
            presentation->Navigate(Keire::RuntimeUiNavigation::Cancel);
        if (ui.KeyPressed(Keire::UiKey::Tab))
        {
            presentation->Navigate(ui.ShiftDown() ? Keire::RuntimeUiNavigation::Previous
                                                  : Keire::RuntimeUiNavigation::Next);
        }
        if (presentation->TextInputFocused())
            return;
        if (ui.KeyPressed(Keire::UiKey::Left))
            presentation->Navigate(Keire::RuntimeUiNavigation::Left);
        if (ui.KeyPressed(Keire::UiKey::Right))
            presentation->Navigate(Keire::RuntimeUiNavigation::Right);
        if (ui.KeyPressed(Keire::UiKey::Up))
            presentation->Navigate(Keire::RuntimeUiNavigation::Up);
        if (ui.KeyPressed(Keire::UiKey::Down))
            presentation->Navigate(Keire::RuntimeUiNavigation::Down);
    }
} // namespace KeireEditor
