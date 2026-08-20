#pragma once

#include "Keire/Ref.h"

namespace Keire
{
    class ScenePresentationRuntime;
    class UiFrame;
    class Window;
    class WindowSystem;
} // namespace Keire

namespace KeireEditor
{
    void RouteRuntimeUiKeyboard(Keire::UiFrame& ui, const Keire::Ref<Keire::ScenePresentationRuntime>& presentation,
                                const Keire::Ref<Keire::WindowSystem>& windows,
                                const Keire::Ref<Keire::Window>& window);
} // namespace KeireEditor
