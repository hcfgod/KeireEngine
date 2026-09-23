#pragma once

#include "Keire/Ui.h"
#include <imgui.h>

namespace Keire::Detail
{
    [[nodiscard]] inline ImGuiWindowFlags ToImGuiWindowFlags(const UiWindowOptions options) noexcept
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;
        if (options.MenuBar)
            flags |= ImGuiWindowFlags_MenuBar;
        if (options.NoTitleBar)
            flags |= ImGuiWindowFlags_NoTitleBar;
        if (options.NoResize)
            flags |= ImGuiWindowFlags_NoResize;
        if (options.NoMove)
            flags |= ImGuiWindowFlags_NoMove;
        if (options.NoCollapse)
            flags |= ImGuiWindowFlags_NoCollapse;
        if (options.NoSavedSettings)
            flags |= ImGuiWindowFlags_NoSavedSettings;
        if (options.NoScrollbar)
            flags |= ImGuiWindowFlags_NoScrollbar;
        if (options.NoScrollWithMouse)
            flags |= ImGuiWindowFlags_NoScrollWithMouse;
        if (options.AlwaysVerticalScrollbar)
            flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
        return flags;
    }
} // namespace Keire::Detail
