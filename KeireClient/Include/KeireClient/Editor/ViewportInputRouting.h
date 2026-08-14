#pragma once

namespace KeireEditor
{
    [[nodiscard]] constexpr bool GameViewportOwnsRuntimeInput(const bool playActive, const bool applicationFocused,
                                                              const bool panelFocused, const bool captureRequested,
                                                              const bool captureSuspended) noexcept
    {
        if (!playActive || !applicationFocused || captureSuspended)
            return false;
        if (captureRequested)
            return true;
        // Requesting focus when Play starts must be enough to route keyboard/gamepad input even when the pointer is
        // still over another panel. A click on the viewport focuses its panel and restores ownership after Escape.
        return panelFocused;
    }
} // namespace KeireEditor
