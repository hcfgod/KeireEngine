#include "KeireRuntimeInternal/RuntimeUiInput.h"

#include "Keire/Scenes/ScenePresentationRuntime.h"
#include "Keire/Window.h"
#include "KeireInternal/WindowInternal.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

namespace KeireRuntime
{
    void ProcessRuntimeUiEvent(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation, const SDL_Event& event,
                               const float scaleX, const float scaleY, RuntimeUiPointerState& pointer)
    {
        if (!presentation)
            return;
        const auto move = [&](const float x, const float y)
        {
            pointer.X = x * scaleX;
            pointer.Y = y * scaleY;
            presentation->PointerMove(pointer.X, pointer.Y);
        };
        const auto navigate = [&](const Keire::RuntimeUiNavigation navigation) { presentation->Navigate(navigation); };

        if (event.type == SDL_EVENT_MOUSE_MOTION)
            move(event.motion.x, event.motion.y);
        else if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            move(event.wheel.mouse_x, event.wheel.mouse_y);
            const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
            presentation->PointerWheel(pointer.X, pointer.Y, event.wheel.x * direction, event.wheel.y * direction);
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            move(event.button.x, event.button.y);
            const bool pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                pointer.PrimaryDown = pressed;
                presentation->PointerButton(pointer.X, pointer.Y, Keire::RuntimeUiPointerButton::Primary, pressed);
            }
            else if (event.button.button == SDL_BUTTON_RIGHT)
            {
                pointer.SecondaryDown = pressed;
                presentation->PointerButton(pointer.X, pointer.Y, Keire::RuntimeUiPointerButton::Secondary, pressed);
            }
        }
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            if (pointer.PrimaryDown)
                presentation->PointerButton(pointer.X, pointer.Y, Keire::RuntimeUiPointerButton::Primary, false);
            if (pointer.SecondaryDown)
                presentation->PointerButton(pointer.X, pointer.Y, Keire::RuntimeUiPointerButton::Secondary, false);
            pointer.PrimaryDown = false;
            pointer.SecondaryDown = false;
        }
        else if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text)
            presentation->TextInput(event.text.text);
        else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            switch (event.key.key)
            {
            case SDLK_BACKSPACE:
                (void)presentation->KeyInput(Keire::RuntimeUiKey::Backspace);
                break;
            case SDLK_DELETE:
                (void)presentation->KeyInput(Keire::RuntimeUiKey::Delete);
                break;
            case SDLK_RETURN:
                if (!presentation->KeyInput(Keire::RuntimeUiKey::Enter))
                    navigate(Keire::RuntimeUiNavigation::Accept);
                break;
            case SDLK_ESCAPE:
                if (!presentation->KeyInput(Keire::RuntimeUiKey::Escape))
                    navigate(Keire::RuntimeUiNavigation::Cancel);
                break;
            case SDLK_TAB:
                navigate((event.key.mod & SDL_KMOD_SHIFT) != 0 ? Keire::RuntimeUiNavigation::Previous
                                                               : Keire::RuntimeUiNavigation::Next);
                break;
            case SDLK_LEFT:
                navigate(Keire::RuntimeUiNavigation::Left);
                break;
            case SDLK_RIGHT:
                navigate(Keire::RuntimeUiNavigation::Right);
                break;
            case SDLK_UP:
                navigate(Keire::RuntimeUiNavigation::Up);
                break;
            case SDLK_DOWN:
                navigate(Keire::RuntimeUiNavigation::Down);
                break;
            default:
                break;
            }
        }
        else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        {
            switch (static_cast<SDL_GamepadButton>(event.gbutton.button))
            {
            case SDL_GAMEPAD_BUTTON_SOUTH:
                navigate(Keire::RuntimeUiNavigation::Accept);
                break;
            case SDL_GAMEPAD_BUTTON_EAST:
                navigate(Keire::RuntimeUiNavigation::Cancel);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                navigate(Keire::RuntimeUiNavigation::Left);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                navigate(Keire::RuntimeUiNavigation::Right);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_UP:
                navigate(Keire::RuntimeUiNavigation::Up);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                navigate(Keire::RuntimeUiNavigation::Down);
                break;
            default:
                break;
            }
        }
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
