#pragma once

#include "Keire/Window.h"

#include <SDL3/SDL_events.h>

struct SDL_Window;

namespace Keire
{
    class WindowSystemInternalAccess final
    {
      public:
        using EventSink = void (*)(void*, const SDL_Event&) noexcept;

        [[nodiscard]] static SDL_Window* NativeWindow(WindowSystem& system, WindowId id);
        static void SetEventSink(WindowSystem& system, void* context, EventSink sink);
    };
} // namespace Keire
