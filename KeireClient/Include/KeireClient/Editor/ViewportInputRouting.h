#pragma once

namespace KeireEditor
{
    [[nodiscard]] constexpr bool GameViewportOwnsRuntimeInput(const bool playActive, const bool focused,
                                                              const bool hovered) noexcept
    {
        return playActive && focused && hovered;
    }
} // namespace KeireEditor
