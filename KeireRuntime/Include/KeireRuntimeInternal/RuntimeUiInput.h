#pragma once

#include "Keire/Ref.h"

#include <SDL3/SDL_events.h>

#include <array>
#include <cstdint>

namespace Keire
{
    class ScenePresentationRuntime;
    class SceneRuntimeWorld;
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
        bool MiddleDown = false;
        Keire::Ref<Keire::ScenePresentationRuntime> HoveredPresentation;
        std::array<Keire::Ref<Keire::ScenePresentationRuntime>, 3> PointerCaptures;
    };

    [[nodiscard]] bool ProcessRuntimeUiEvent(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                             const SDL_Event& event, float scaleX, float scaleY,
                                             RuntimeUiPointerState& pointer);
    [[nodiscard]] Keire::Ref<Keire::ScenePresentationRuntime>
    ProcessRuntimeUiEventStack(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                               const Keire::Ref<Keire::ScenePresentationRuntime>& fallback, const SDL_Event& event,
                               float scaleX, float scaleY, RuntimeUiPointerState& pointer);
    void SynchronizeRuntimeUiTextInput(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                       const Keire::Ref<Keire::WindowSystem>& windows,
                                       const Keire::Ref<Keire::Window>& window) noexcept;
} // namespace KeireRuntime
