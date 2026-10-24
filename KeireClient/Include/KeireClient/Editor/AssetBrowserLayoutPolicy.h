#pragma once

namespace KeireEditor
{
    [[nodiscard]] constexpr bool UseCompactAssetBrowserChrome(const float availableHeight) noexcept
    {
        // Keep decorative headings, status, and thumbnail controls from consuming the asset area.
        return availableHeight < 240.0F;
    }

    [[nodiscard]] constexpr bool UseAssetBrowserListForHeight(const float availableHeight,
                                                              const float thumbnailSize) noexcept
    {
        constexpr float labelAndCellPadding = 48.0F;
        return availableHeight < thumbnailSize + labelAndCellPadding;
    }
} // namespace KeireEditor
