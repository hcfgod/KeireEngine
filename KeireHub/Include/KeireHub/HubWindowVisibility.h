#pragma once

#include "Keire/Window.h"

namespace KeireHub
{
    inline void RestoreHubWindow(Keire::Window& window)
    {
        // Hidden windows can defer restore requests, retaining their minimized native placement.
        window.SetVisible(true);
        window.Restore();
        window.Raise();
    }
} // namespace KeireHub
