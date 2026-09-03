#pragma once

#include <cstdint>

namespace KeireEditor
{
    enum class EditorPanelMenuAction : std::uint8_t
    {
        Hide,
        ShowAndFocus
    };

    [[nodiscard]] constexpr EditorPanelMenuAction EditorPanelMenuActionFor(const bool visible) noexcept
    {
        return visible ? EditorPanelMenuAction::Hide : EditorPanelMenuAction::ShowAndFocus;
    }
} // namespace KeireEditor
