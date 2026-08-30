#pragma once

#include "Keire/Ref.h"

#include <array>
#include <cstdint>
#include <span>

namespace Keire
{
    class ScenePresentationRuntime;
    class SceneRuntimeWorld;
    class UiFrame;
    struct UiItemRect;
    class Window;
    class WindowSystem;
} // namespace Keire

namespace KeireEditor
{
    struct RuntimeUiPointerRoutingState final
    {
        Keire::Ref<Keire::ScenePresentationRuntime> HoveredPresentation;
        std::array<Keire::Ref<Keire::ScenePresentationRuntime>, 3> PointerCaptures;

        void Reset() noexcept
        {
            HoveredPresentation.Reset();
            for (auto& capture : PointerCaptures)
                capture.Reset();
        }
    };

    struct RuntimeGameUiRoutingPlan final
    {
        bool Pointer = false;
        bool Keyboard = false;
    };

    [[nodiscard]] constexpr RuntimeGameUiRoutingPlan
    ResolveRuntimeGameUiRouting(const bool playActive, const bool hasPresentation,
                                const bool viewportInputActive) noexcept
    {
        return {.Pointer = hasPresentation, .Keyboard = playActive && hasPresentation && viewportInputActive};
    }

    void RouteRuntimeUiPointer(Keire::UiFrame& ui,
                               std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
                               const Keire::UiItemRect& viewport, RuntimeUiPointerRoutingState& state);
    void CancelRuntimeUiPointer(std::span<const Keire::Ref<Keire::ScenePresentationRuntime>> presentations,
                                RuntimeUiPointerRoutingState& state) noexcept;
    void CancelRuntimeUiPointer(const Keire::Ref<Keire::SceneRuntimeWorld>& world,
                                RuntimeUiPointerRoutingState& state) noexcept;
    [[nodiscard]] bool RuntimeUiPresentationHitTest(const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                                    float x, float y) noexcept;
    [[nodiscard]] Keire::Ref<Keire::ScenePresentationRuntime>
    SelectRuntimeUiKeyboardPresentation(const Keire::Ref<Keire::ScenePresentationRuntime>& active) noexcept;
    void RouteRuntimeUiKeyboard(Keire::UiFrame& ui, const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                const Keire::Ref<Keire::WindowSystem>& windows,
                                const Keire::Ref<Keire::Window>& window);
} // namespace KeireEditor
