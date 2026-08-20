#pragma once

#include "Keire/Ref.h"

#include <SDL3/SDL_events.h>

namespace Keire
{
    class ScenePresentationRuntime;
    class Window;
    class WindowSystem;
} // namespace Keire

namespace KeireRuntime
{
    struct RuntimeUiPointerState
    {
        float X = 0.0F;
        float Y = 0.0F;
        bool PrimaryDown = false;
        bool SecondaryDown = false;
    };

    void ProcessRuntimeUiEvent(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation, const SDL_Event& event,
                               float scaleX, float scaleY, RuntimeUiPointerState& pointer);
    void SynchronizeRuntimeUiTextInput(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                       const Keire::Ref<Keire::WindowSystem>& windows,
                                       const Keire::Ref<Keire::Window>& window) noexcept;
} // namespace KeireRuntime
