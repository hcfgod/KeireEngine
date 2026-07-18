#pragma once

#include "Keire/Window.h"

#include <SDL3/SDL_events.h>

#include <cstdint>

struct SDL_Window;

namespace Keire
{
    class WindowSystemInternalAccess final
    {
      public:
        using EventSink = void (*)(void*, const SDL_Event&) noexcept;
        using EventSinkToken = std::uint64_t;

        [[nodiscard]] static SDL_Window* NativeWindow(WindowSystem& system, WindowId id);
        [[nodiscard]] static EventSinkToken AddEventSink(WindowSystem& system, void* context, EventSink sink);
        static void RemoveEventSink(WindowSystem& system, EventSinkToken token) noexcept;
    };
} // namespace Keire
