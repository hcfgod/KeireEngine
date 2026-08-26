#include "KeireRuntimeInternal/RuntimeUiInput.h"

#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Scenes/SceneRuntimeWorld.h"
#include "Keire/Ui/RuntimeUi.h"
#include "Keire/Window.h"
#include "KeireInternal/Scenes/SceneRuntimeRenderingInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{
    constexpr std::size_t PrimaryButton = 0U;
    constexpr std::size_t SecondaryButton = 1U;
    constexpr std::size_t MiddleButton = 2U;
    constexpr std::array PointerButtons{Keire::RuntimeUiPointerButton::Primary,
                                        Keire::RuntimeUiPointerButton::Secondary,
                                        Keire::RuntimeUiPointerButton::Middle};

    [[nodiscard]] bool Contains(const std::vector<Keire::Ref<Keire::ScenePresentationRuntime>>& presentations,
                                const Keire::Ref<Keire::ScenePresentationRuntime>& presentation)
    {
        return presentation && std::ranges::find(presentations, presentation) != presentations.end();
    }

    void LeaveExcept(const std::vector<Keire::Ref<Keire::ScenePresentationRuntime>>& presentations,
                     const Keire::Ref<Keire::ScenePresentationRuntime>& target)
    {
        for (const auto& presentation : presentations)
            if (presentation && presentation != target)
                presentation->PointerLeave();
    }

    void CancelPointerState(const std::vector<Keire::Ref<Keire::ScenePresentationRuntime>>& presentations,
                            KeireRuntime::RuntimeUiPointerState& pointer) noexcept
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
        for (std::size_t index = 0; index < pointer.PointerCaptures.size(); ++index)
        {
            const auto& capture = pointer.PointerCaptures[index];
            if (capture)
                (void)capture->CancelPointerButton(PointerButtons[index]);
        }
        for (const auto& presentation : presentations)
            leave(presentation);
        for (const auto& capture : pointer.PointerCaptures)
            if (capture && !Contains(presentations, capture))
                leave(capture);
        pointer = {};
    }

    [[nodiscard]] Keire::Ref<Keire::ScenePresentationRuntime>
    HitPresentation(const std::vector<Keire::Ref<Keire::ScenePresentationRuntime>>& presentations, const float x,
                    const float y)
    {
        const auto found = std::ranges::find_if(presentations.rbegin(), presentations.rend(),
                                                [x, y](const Keire::Ref<Keire::ScenePresentationRuntime>& presentation)
                                                {
                                                    const auto tree = presentation ? presentation->Ui()
                                                                                   : Keire::Ref<Keire::RuntimeUiTree>{};
                                                    return tree && tree->HitTest(x, y);
                                                });
        return found == presentations.rend() ? Keire::Ref<Keire::ScenePresentationRuntime>{} : *found;
    }

    void RoutePointerMove(const std::vector<Keire::Ref<Keire::ScenePresentationRuntime>>& presentations,
                          KeireRuntime::RuntimeUiPointerState& pointer)
    {
        std::vector<Keire::Ref<Keire::ScenePresentationRuntime>> captures;
        for (std::size_t index = 0; index < pointer.PointerCaptures.size(); ++index)
        {
            auto& capture = pointer.PointerCaptures[index];
            if (capture && !Contains(presentations, capture))
            {
                (void)capture->CancelPointerButton(PointerButtons[index]);
                capture.Reset();
            }
            if (capture && std::ranges::find(captures, capture) == captures.end())
                captures.push_back(capture);
        }
        auto target = captures.empty() ? HitPresentation(presentations, pointer.X, pointer.Y) : captures.front();
        for (const auto& presentation : presentations)
        {
            if (presentation && presentation != target && std::ranges::find(captures, presentation) == captures.end())
                presentation->PointerLeave();
        }
        if (captures.empty())
        {
            if (target)
                target->PointerMove(pointer.X, pointer.Y);
        }
        else
        {
            for (const auto& capture : captures)
                capture->PointerMove(pointer.X, pointer.Y);
        }
        pointer.HoveredPresentation = target;
    }

    [[nodiscard]] std::optional<std::size_t> ButtonIndex(const std::uint8_t button) noexcept
    {
        if (button == SDL_BUTTON_LEFT)
            return PrimaryButton;
        if (button == SDL_BUTTON_RIGHT)
            return SecondaryButton;
        if (button == SDL_BUTTON_MIDDLE)
            return MiddleButton;
        return std::nullopt;
    }

    [[nodiscard]] Keire::RuntimeUiPointerButton PointerButton(const std::uint8_t button) noexcept
    {
        if (button == SDL_BUTTON_RIGHT)
            return Keire::RuntimeUiPointerButton::Secondary;
        if (button == SDL_BUTTON_MIDDLE)
            return Keire::RuntimeUiPointerButton::Middle;
        return Keire::RuntimeUiPointerButton::Primary;
    }
} // namespace

namespace KeireRuntime
{
    bool ProcessRuntimeUiEvent(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation, const SDL_Event& event,
                               const float scaleX, const float scaleY, RuntimeUiPointerState& pointer)
    {
        if (!presentation)
            return false;
        const auto move = [&](const float x, const float y)
        {
            pointer.X = x * scaleX;
            pointer.Y = y * scaleY;
            presentation->PointerMove(pointer.X, pointer.Y);
        };
        const auto navigate = [&](const Keire::RuntimeUiNavigation navigation)
        {
            presentation->Navigate(navigation);
            return true;
        };

        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            move(event.motion.x, event.motion.y);
            return false;
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            move(event.wheel.mouse_x, event.wheel.mouse_y);
            const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
            return presentation->PointerWheel(pointer.X, pointer.Y, event.wheel.x * direction,
                                              event.wheel.y * direction);
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            move(event.button.x, event.button.y);
            const auto index = ButtonIndex(event.button.button);
            if (!index)
                return false;
            const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (*index == PrimaryButton)
                pointer.PrimaryDown = pressed;
            else if (*index == SecondaryButton)
                pointer.SecondaryDown = pressed;
            else
                pointer.MiddleDown = pressed;
            return presentation->PointerButton(pointer.X, pointer.Y, PointerButton(event.button.button), pressed);
        }
        if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text)
        {
            if (!presentation->TextInputFocused())
                return false;
            presentation->TextInput(event.text.text);
            return true;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
            case SDLK_BACKSPACE:
                return presentation->KeyInput(Keire::RuntimeUiKey::Backspace);
            case SDLK_DELETE:
                return presentation->KeyInput(Keire::RuntimeUiKey::Delete);
            case SDLK_RETURN:
                return presentation->KeyInput(Keire::RuntimeUiKey::Enter) ||
                       navigate(Keire::RuntimeUiNavigation::Accept);
            case SDLK_ESCAPE:
                return presentation->KeyInput(Keire::RuntimeUiKey::Escape) ||
                       navigate(Keire::RuntimeUiNavigation::Cancel);
            case SDLK_TAB:
                return navigate((event.key.mod & SDL_KMOD_SHIFT) != 0 ? Keire::RuntimeUiNavigation::Previous
                                                                      : Keire::RuntimeUiNavigation::Next);
            case SDLK_LEFT:
                return navigate(Keire::RuntimeUiNavigation::Left);
            case SDLK_RIGHT:
                return navigate(Keire::RuntimeUiNavigation::Right);
            case SDLK_UP:
                return navigate(Keire::RuntimeUiNavigation::Up);
            case SDLK_DOWN:
                return navigate(Keire::RuntimeUiNavigation::Down);
            default:
                return false;
            }
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        {
            switch (static_cast<SDL_GamepadButton>(event.gbutton.button))
            {
            case SDL_GAMEPAD_BUTTON_SOUTH:
                return navigate(Keire::RuntimeUiNavigation::Accept);
            case SDL_GAMEPAD_BUTTON_EAST:
                return navigate(Keire::RuntimeUiNavigation::Cancel);
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                return navigate(Keire::RuntimeUiNavigation::Left);
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                return navigate(Keire::RuntimeUiNavigation::Right);
            case SDL_GAMEPAD_BUTTON_DPAD_UP:
                return navigate(Keire::RuntimeUiNavigation::Up);
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                return navigate(Keire::RuntimeUiNavigation::Down);
            default:
                return false;
            }
        }
        return false;
    }

    Keire::Ref<Keire::ScenePresentationRuntime>
    ProcessRuntimeUiEventStack(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                               const Keire::Ref<Keire::ScenePresentationRuntime>& fallback, const SDL_Event& event,
                               const float scaleX, const float scaleY, RuntimeUiPointerState& pointer)
    {
        std::vector<Keire::Ref<Keire::ScenePresentationRuntime>> presentations;
        if (world)
        {
            for (const auto& session : world->Sessions())
                if (const auto presentation =
                        session ? session->Presentation() : Keire::Ref<Keire::ScenePresentationRuntime>{})
                    presentations.push_back(presentation);
        }
        const auto active = world ? Keire::Internal::ActiveRuntimePresentation(world) : fallback;
        const bool keyboardEvent = event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN ||
                                   event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (keyboardEvent)
        {
            const auto focusedActive = active && active->FocusedUiEntity()
                                           ? active
                                           : Keire::Ref<Keire::ScenePresentationRuntime>{};
            (void)ProcessRuntimeUiEvent(focusedActive, event, scaleX, scaleY, pointer);
            return focusedActive;
        }

        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            CancelPointerState(presentations, pointer);
            return active;
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            pointer.X = event.motion.x * scaleX;
            pointer.Y = event.motion.y * scaleY;
            RoutePointerMove(presentations, pointer);
            return active;
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            pointer.X = event.wheel.mouse_x * scaleX;
            pointer.Y = event.wheel.mouse_y * scaleY;
            const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
            Keire::Ref<Keire::ScenePresentationRuntime> handled;
            for (auto current = presentations.rbegin(); current != presentations.rend(); ++current)
            {
                if (*current && (*current)->PointerWheel(pointer.X, pointer.Y, event.wheel.x * direction,
                                                         event.wheel.y * direction))
                {
                    handled = *current;
                    break;
                }
            }
            LeaveExcept(presentations, handled);
            if (handled)
                handled->PointerMove(pointer.X, pointer.Y);
            pointer.HoveredPresentation = handled;
            return active;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            pointer.X = event.button.x * scaleX;
            pointer.Y = event.button.y * scaleY;
            const auto index = ButtonIndex(event.button.button);
            if (!index)
                return active;
            const auto button = PointerButton(event.button.button);
            const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (pressed)
            {
                auto& capture = pointer.PointerCaptures[*index];
                Keire::Ref<Keire::ScenePresentationRuntime> handled = capture;
                if (handled)
                {
                    if (!handled->PointerButton(pointer.X, pointer.Y, button, true))
                        handled.Reset();
                }
                else
                {
                    for (auto current = presentations.rbegin(); current != presentations.rend(); ++current)
                    {
                        if (*current && (*current)->PointerButton(pointer.X, pointer.Y, button, true))
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
                RoutePointerMove(presentations, pointer);
            }
            else if (auto& capture = pointer.PointerCaptures[*index]; capture)
            {
                capture->PointerMove(pointer.X, pointer.Y);
                (void)capture->PointerButton(pointer.X, pointer.Y, button, false);
                capture.Reset();
                RoutePointerMove(presentations, pointer);
            }
            if (*index == PrimaryButton)
                pointer.PrimaryDown = pressed;
            else if (*index == SecondaryButton)
                pointer.SecondaryDown = pressed;
            else
                pointer.MiddleDown = pressed;
        }
        return active;
    }

    void SynchronizeRuntimeUiTextInput(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                       const Keire::Ref<Keire::WindowSystem>& windows,
                                       const Keire::Ref<Keire::Window>& window) noexcept
    {
        try
        {
            if (windows && window)
            {
                (void)Keire::WindowSystemInternalAccess::SetTextInput(*windows, window->Id(),
                                                                      presentation && presentation->TextInputFocused());
            }
        }
        catch (...)
        {
        }
    }
} // namespace KeireRuntime
