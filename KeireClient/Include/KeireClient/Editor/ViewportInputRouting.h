#pragma once

namespace KeireEditor
{
    [[nodiscard]] constexpr bool GameViewportOwnsRuntimeInput(const bool playActive, const bool applicationFocused,
                                                              const bool panelFocused, const bool hovered,
                                                              const bool alreadyEngaged, const bool captureRequested,
                                                              const bool captureSuspended) noexcept
    {
        if (!playActive || !applicationFocused || captureSuspended)
            return false;
        if (captureRequested)
            return true;
        return panelFocused && (hovered || alreadyEngaged);
    }
} // namespace KeireEditor
